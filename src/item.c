#include <reduct/atom.h>
#include <reduct/closure.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/item.h>

static inline void reduct_item_free_external(reduct_item_t* item)
{
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_ATOM:
        if (item->atom.flags & REDUCT_ATOM_FLAG_SCHEMA && item->atom.schema != NULL)
        {
            free(item->atom.schema);
            item->atom.schema = NULL;
        }
        break;
    case REDUCT_ITEM_TYPE_ATOM_STACK:
        if (item->atomStack.data != NULL)
        {
            free(item->atomStack.data);
            item->atomStack.data = NULL;
        }
        break;
    case REDUCT_ITEM_TYPE_FUNCTION:
        if (item->function.insts != NULL)
        {
            free(item->function.insts);
            item->function.insts = NULL;
        }
        if (item->function.positions != NULL)
        {
            free(item->function.positions);
            item->function.positions = NULL;
        }
        if (item->function.constants != NULL)
        {
            free(item->function.constants);
            item->function.constants = NULL;
        }
        break;
    case REDUCT_ITEM_TYPE_CLOSURE:
        if (item->closure.constants != NULL && item->closure.constants != item->closure.smallConstants)
        {
            free(item->closure.constants);
            item->closure.constants = item->closure.smallConstants;
        }
        break;
    default:
        break;
    }
}

REDUCT_API void reduct_item_env_init(reduct_item_env_t* env)
{
    assert(env != NULL);
    env->prevBlockCount = 0;
    env->blockCount = 0;
    env->block = NULL;
    reduct_rwmutex_init(&env->mutex);
}

REDUCT_API void reduct_item_env_deinit(reduct_item_env_t* env)
{
    assert(env != NULL);
    reduct_item_block_t* block = env->block;
    while (block != NULL)
    {
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            reduct_item_free_external(&block->items[i]);
        }
        reduct_item_block_t* next = block->next;
        free(block->allocated);
        block = next;
    }
    env->block = NULL;
    reduct_rwmutex_destroy(&env->mutex);
}

REDUCT_API void reduct_item_state_init(reduct_item_state_t* state)
{
    assert(state != NULL);
    state->freeCount = 0;
    state->freeList = NULL;
}

REDUCT_API void reduct_item_state_deinit(reduct_item_state_t* state)
{
    assert(state != NULL);
    state->freeCount = 0;
    state->freeList = NULL;
}

static inline void reduct_item_init(reduct_item_t* item)
{
    item->type = REDUCT_ITEM_TYPE_NONE;
    item->flags = 0;
    item->inputId = REDUCT_INPUT_ID_NONE;
    item->position = 0;
}

static inline reduct_item_t* reduct_item_pop_free_list(reduct_t* reduct)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct->item.freeList;
    reduct->item.freeList = item->free;
    reduct->item.freeCount--;
    return item;
}

REDUCT_API reduct_item_t* reduct_item_new(reduct_t* reduct)
{
    assert(reduct != NULL);

    if (REDUCT_LIKELY(reduct->item.freeList != NULL))
    {
        return reduct_item_pop_free_list(reduct);
    }

    void* allocated = calloc(1, sizeof(reduct_item_block_t));
    if (allocated == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    reduct_item_block_t* block = (reduct_item_block_t*)REDUCT_ROUND_UP((size_t)allocated, REDUCT_ALIGNMENT);
    block->allocated = allocated;

    for (size_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
    {
        reduct_item_init(&block->items[i]);
    }

    reduct_rwmutex_write_lock(&reduct->env->item.mutex);
    reduct->env->item.blockCount++;
    block->next = reduct->env->item.block;
    reduct->env->item.block = block;
    reduct_rwmutex_write_unlock(&reduct->env->item.mutex);

    for (size_t i = 1; i < REDUCT_ITEM_BLOCK_MAX - 1; i++)
    {
        block->items[i].free = &block->items[i + 1];
    }
    block->items[REDUCT_ITEM_BLOCK_MAX - 1].free = NULL;
    reduct->item.freeCount += REDUCT_ITEM_BLOCK_MAX - 1;
    reduct->item.freeList = &block->items[1];

    return &block->items[0];
}

REDUCT_API void reduct_item_deinit(reduct_t* reduct, reduct_item_t* item)
{
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* atom = &item->atom;

        reduct_rwmutex_write_lock(&reduct->env->atom.mutex);
        if (reduct->env->atom.map != NULL && atom->index != REDUCT_ATOM_INDEX_NONE)
        {
            reduct->env->atom.map[atom->index] = REDUCT_ATOM_TOMBSTONE;
            reduct->env->atom.tombstones++;
            reduct->env->atom.size--;
        }
        reduct_rwmutex_write_unlock(&reduct->env->atom.mutex);
    }
    break;
    case REDUCT_ITEM_TYPE_ATOM_STACK:
    {
        reduct_atom_stack_t* stack = &item->atomStack;
        if (stack->next != NULL)
        {
            stack->next->prev = stack->prev;
        }
        if (stack->prev != NULL)
        {
            stack->prev->next = stack->next;
        }
        else if (reduct->atom.atomStack == stack)
        {
            reduct->atom.atomStack = stack->next;
        }
        break;
    }
    case REDUCT_ITEM_TYPE_FUNCTION:
    case REDUCT_ITEM_TYPE_CLOSURE:
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
    case REDUCT_ITEM_TYPE_RVSDG_REGION:
    case REDUCT_ITEM_TYPE_RVSDG_USER:
    case REDUCT_ITEM_TYPE_RVSDG_ORIGIN:
        break;
    default:
        break;
    }

    reduct_item_free_external(item);
    reduct_item_init(item);

#ifndef NDEBUG
    memset(&item->atom, 0xFE, sizeof(reduct_item_t) - offsetof(reduct_item_t, atom));
#endif
}

REDUCT_API void reduct_item_free(reduct_t* reduct, reduct_item_t* item)
{
    assert(reduct != NULL);
    assert(item != NULL);

    reduct_item_deinit(reduct, item);

    reduct->item.freeCount++;
    item->free = reduct->item.freeList;
    reduct->item.freeList = item;
}

REDUCT_API const char* reduct_item_type_str(reduct_item_t* item)
{
    if (item == NULL)
    {
        return "none";
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_NONE:
        return "none";
    case REDUCT_ITEM_TYPE_ATOM:
        if (reduct_atom_is_number(&item->atom))
        {
            return "number";
        }
        return "atom";
    case REDUCT_ITEM_TYPE_ATOM_STACK:
        return "atom stack";
    case REDUCT_ITEM_TYPE_LIST:
        return "list";
    case REDUCT_ITEM_TYPE_LIST_NODE:
        return "list node";
    case REDUCT_ITEM_TYPE_FUNCTION:
        return "function";
    case REDUCT_ITEM_TYPE_CLOSURE:
        return "closure";
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
        return "ir node";
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
        return "ir edge";
    case REDUCT_ITEM_TYPE_RVSDG_REGION:
        return "ir region";
    case REDUCT_ITEM_TYPE_RVSDG_USER:
        return "ir user";
    case REDUCT_ITEM_TYPE_RVSDG_ORIGIN:
        return "ir origin";
    default:
        return "unknown";
    }
}
