#include <reduct/atom.h>
#include <reduct/closure.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/item.h>

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

    reduct_item_t* item = reduct->freeList;
    reduct->freeList = item->free;
    reduct->freeCount--;
    return item;
}

REDUCT_API reduct_item_t* reduct_item_new(reduct_t* reduct)
{
    assert(reduct != NULL);

    if (REDUCT_LIKELY(reduct->freeList != NULL))
    {
        return reduct_item_pop_free_list(reduct);
    }

    reduct_item_t* item = NULL;
    void* allocated = calloc(1, sizeof(reduct_item_block_t));
    if (allocated == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    reduct_item_block_t* block = (reduct_item_block_t*)REDUCT_ROUND_UP((size_t)allocated, REDUCT_ALIGNMENT);
    block->allocated = allocated;
    reduct->blockCount++;

    for (size_t i = 1; i < REDUCT_ITEM_BLOCK_MAX - 1; i++)
    {
        block->items[i].free = &block->items[i + 1];
    }
    block->items[REDUCT_ITEM_BLOCK_MAX - 1].free = NULL;
    reduct->freeCount += REDUCT_ITEM_BLOCK_MAX - 1;

    reduct->freeList = &block->items[1];
    block->next = reduct->block;
    reduct->block = block;

    item = &block->items[0];
    return item;
}

REDUCT_API void reduct_item_deinit(reduct_t* reduct, reduct_item_t* item)
{
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* atom = &item->atom;
        if (reduct->atomMap != NULL && atom->index != REDUCT_ATOM_INDEX_NONE)
        {
            reduct->atomMap[atom->index] = REDUCT_ATOM_TOMBSTONE;
            reduct->atomMapTombstones++;
            reduct->atomMapSize--;
        }
        if (atom->flags & REDUCT_ATOM_FLAG_SCHEMA)
        {
            if (atom->schema != NULL)
            {
                free(atom->schema);
            }
        }
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
        else if (reduct->atomStack == stack)
        {
            reduct->atomStack = stack->next;
        }
        if (stack->data != NULL)
        {
            free(stack->data);
        }
    }
    break;
    case REDUCT_ITEM_TYPE_FUNCTION:
    {
        reduct_function_t* func = &item->function;
        if (func->insts != NULL)
        {
            free(func->insts);
        }
        if (func->positions != NULL)
        {
            free(func->positions);
        }
        if (func->constants != NULL)
        {
            free(func->constants);
        }
    }
    break;
    case REDUCT_ITEM_TYPE_CLOSURE:
    {
        reduct_closure_t* closure = &item->closure;
        if (closure->constants != closure->smallConstants)
        {
            free(closure->constants);
        }
    }
    break;
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
    case REDUCT_ITEM_TYPE_RVSDG_REGION:
    case REDUCT_ITEM_TYPE_RVSDG_USER:
    case REDUCT_ITEM_TYPE_RVSDG_ORIGIN:
        break;
    default:
        break;
    }

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

    reduct->freeCount++;
    item->free = reduct->freeList;
    reduct->freeList = item;
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
