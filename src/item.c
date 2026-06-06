#include "reduct/sync.h"
#include <reduct/atom.h>
#include <reduct/closure.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/item.h>
#include <threads.h>

REDUCT_API void reduct_item_env_init(reduct_item_env_t* env)
{
    assert(env != NULL);
    env->prevBlockCount = 0;
    env->blockCount = 0;
    env->block = NULL;
    reduct_rwmutex_init(&env->mutex);
    env->globalFreeList = NULL;
    env->globalFreeCount = 0;
}

REDUCT_API void reduct_item_env_deinit(reduct_t* reduct, reduct_item_env_t* env)
{
    assert(env != NULL);
    reduct_item_block_t* block = env->block;
    while (block != NULL)
    {
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            reduct_item_free(reduct, &block->items[i]);
        }
        reduct_item_block_t* next = block->next;
        free(block->allocated);
        block = next;
    }
    env->block = NULL;
    reduct_rwmutex_destroy(&env->mutex);
    env->globalFreeList = NULL;
    env->globalFreeCount = 0;
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
    atomic_init(&item->flags, REDUCT_ITEM_FLAG_NONE);
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

    reduct_rwmutex_write_lock(&reduct->env->item.mutex);
    if (REDUCT_UNLIKELY(reduct->env->item.globalFreeList != NULL))
    {
        reduct_item_t* head = reduct->env->item.globalFreeList;
        reduct_item_t* tail = head;
        size_t count = 1;

        while (count < REDUCT_ITEM_BLOCK_MAX && tail->free != NULL)
        {
            tail = tail->free;
            count++;
        }

        reduct->env->item.globalFreeList = tail->free;
        reduct->env->item.globalFreeCount -= count;
        reduct_rwmutex_write_unlock(&reduct->env->item.mutex);

        tail->free = NULL;
        reduct->item.freeList = head->free;
        reduct->item.freeCount = count - 1;

        return head;
    }
    reduct_rwmutex_write_unlock(&reduct->env->item.mutex);

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

    reduct_gc_request(&reduct->env->gc);

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
        if (atom->flags & REDUCT_ATOM_FLAG_SCHEMA && atom->schema != NULL)
        {
            free(atom->schema);
            atom->schema = NULL;
        }
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
        if (stack->data != NULL)
        {
            free(stack->data);
            stack->data = NULL;
        }
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
    }
    break;
    case REDUCT_ITEM_TYPE_FUTURE:
    {
        if (item->future.error != NULL)
        {
            free(item->future.error);
            item->future.error = NULL;
        }
        if (item->future.argv != item->future.smallArgv)
        {
            free(item->future.argv);
            item->future.argv = item->future.smallArgv;
        }
    }
    break;
    case REDUCT_ITEM_TYPE_FUNCTION:
    {
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
    }
    break;
    case REDUCT_ITEM_TYPE_CLOSURE:
    {
        if (item->closure.constants != NULL && item->closure.constants != item->closure.smallConstants)
        {
            free(item->closure.constants);
            item->closure.constants = item->closure.smallConstants;
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
    case REDUCT_ITEM_TYPE_FUTURE:
        return "future";
    default:
        return "unknown";
    }
}

static inline void reduct_item_mark_node(uint32_t shift, reduct_list_node_t* node);

static inline void reduct_item_mark_node_contents(uint32_t shift, reduct_list_node_t* node)
{
    if (shift == 0)
    {
        for (uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_handle_t handle = node->handles[i];
            if (REDUCT_HANDLE_IS_ITEM(handle))
            {
                reduct_item_mark(REDUCT_HANDLE_TO_ITEM(handle));
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_item_mark_node(shift - REDUCT_LIST_BITS, node->children[i]);
        }
    }
}

static inline void reduct_item_mark_node(uint32_t shift, reduct_list_node_t* node)
{
    if (node == NULL)
    {
        return;
    }

    reduct_item_t* item = REDUCT_CONTAINER_OF(node, reduct_item_t, listNode);
    if (atomic_fetch_or(&item->flags, REDUCT_ITEM_FLAG_MARKED) & REDUCT_ITEM_FLAG_MARKED)
    {
        return;
    }

    reduct_item_mark_node_contents(shift, node);
}

static inline void reduct_item_mark_list(reduct_list_t* list)
{
    reduct_item_mark_node(list->shift, list->root);
    reduct_item_mark_node_contents(0, &list->tail);
}

static inline void reduct_item_mark_atom(reduct_atom_t* atom)
{
    if (atom->flags & REDUCT_ATOM_FLAG_LARGE && atom->stack != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(atom->stack, reduct_item_t, atomStack));
    }
}

static inline void reduct_item_mark_rvsdg_node(reduct_rvsdg_node_t* node)
{
    if (node->parent != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(node->parent, reduct_item_t, rvsdgRegion));
    }

    reduct_rvsdg_user_t* user = node->firstInput;
    while (user != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(user, reduct_item_t, rvsdgUser));
        user = user->next;
    }

    if (node->output != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(node->output, reduct_item_t, rvsdgOrigin));
    }

    reduct_rvsdg_region_t* region = node->firstRegion;
    while (region != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(region, reduct_item_t, rvsdgRegion));
        region = region->next;
    }

    if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST && REDUCT_HANDLE_IS_ITEM(node->constant))
    {
        reduct_item_mark(REDUCT_HANDLE_TO_ITEM(node->constant));
    }
}

static inline void reduct_item_mark_rvsdg_edge(reduct_rvsdg_edge_t* edge)
{
    if (edge->origin != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(edge->origin, reduct_item_t, rvsdgOrigin));
    }

    if (edge->user != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(edge->user, reduct_item_t, rvsdgUser));
    }
}

static inline void reduct_item_mark_rvsdg_region(reduct_rvsdg_region_t* region)
{
    if (region->parent != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(region->parent, reduct_item_t, rvsdgNode));
    }

    reduct_rvsdg_origin_t* arg = region->firstArgument;
    while (arg != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(arg, reduct_item_t, rvsdgOrigin));
        arg = arg->next;
    }

    if (region->result != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(region->result, reduct_item_t, rvsdgUser));
    }

    reduct_rvsdg_node_t* node = region->firstNode;
    while (node != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(node, reduct_item_t, rvsdgNode));
        node = node->next;
    }
}

static inline void reduct_item_mark_rvsdg_user(reduct_rvsdg_user_t* user)
{
    if (user->ownerKind == REDUCT_RVSDG_OWNER_NODE && user->node != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(user->node, reduct_item_t, rvsdgNode));
    }
    else if (user->ownerKind == REDUCT_RVSDG_OWNER_REGION && user->region != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(user->region, reduct_item_t, rvsdgRegion));
    }

    if (user->edge != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(user->edge, reduct_item_t, rvsdgEdge));
    }
}

static inline void reduct_item_mark_rvsdg_origin(reduct_rvsdg_origin_t* origin)
{
    if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE && origin->node != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(origin->node, reduct_item_t, rvsdgNode));
    }
    else if (origin->ownerKind == REDUCT_RVSDG_OWNER_REGION && origin->region != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(origin->region, reduct_item_t, rvsdgRegion));
    }

    reduct_rvsdg_edge_t* edge = origin->edges;
    while (edge != NULL)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(edge, reduct_item_t, rvsdgEdge));
        edge = edge->next;
    }
}

REDUCT_API void reduct_item_mark(reduct_item_t* item)
{
    if (REDUCT_UNLIKELY(item == NULL))
    {
        return;
    }

    if (atomic_fetch_or(&item->flags, REDUCT_ITEM_FLAG_MARKED) & REDUCT_ITEM_FLAG_MARKED)
    {
        return;
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
        reduct_item_mark_list(&item->list);
        break;
    case REDUCT_ITEM_TYPE_LIST_NODE:
        reduct_item_mark_node(0, &item->listNode);
        break;
    case REDUCT_ITEM_TYPE_ATOM:
        reduct_item_mark_atom(&item->atom);
        break;
    case REDUCT_ITEM_TYPE_FUNCTION:
        for (uint16_t i = 0; i < item->function.constantCount; i++)
        {
            if (item->function.constants[i].type == REDUCT_CONST_SLOT_TYPE_STATIC &&
                REDUCT_HANDLE_IS_ITEM(item->function.constants[i].handle))
            {
                reduct_item_mark(REDUCT_HANDLE_TO_ITEM(item->function.constants[i].handle));
            }
        }
        break;
    case REDUCT_ITEM_TYPE_CLOSURE:
        reduct_item_mark(REDUCT_CONTAINER_OF(item->closure.function, reduct_item_t, function));
        reduct_closure_t* closure = &item->closure;
        for (uint16_t i = 0; i < closure->function->constantCount; i++)
        {
            if (REDUCT_HANDLE_IS_ITEM(closure->constants[i]))
            {
                reduct_item_mark(REDUCT_HANDLE_TO_ITEM(closure->constants[i]));
            }
        }
        break;
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
        reduct_item_mark_rvsdg_node(&item->rvsdgNode);
        break;
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
        reduct_item_mark_rvsdg_edge(&item->rvsdgEdge);
        break;
    case REDUCT_ITEM_TYPE_RVSDG_REGION:
        reduct_item_mark_rvsdg_region(&item->rvsdgRegion);
        break;
    case REDUCT_ITEM_TYPE_RVSDG_USER:
        reduct_item_mark_rvsdg_user(&item->rvsdgUser);
        break;
    case REDUCT_ITEM_TYPE_RVSDG_ORIGIN:
        reduct_item_mark_rvsdg_origin(&item->rvsdgOrigin);
        break;
    case REDUCT_ITEM_TYPE_FUTURE:
    {
        reduct_future_t* future = &item->future;
        if (REDUCT_HANDLE_IS_ITEM(future->callable))
        {
            reduct_item_mark(REDUCT_HANDLE_TO_ITEM(future->callable));
        }
        if (atomic_load_explicit(&future->done, memory_order_acquire) && REDUCT_HANDLE_IS_ITEM(future->result))
        {
            reduct_item_mark(REDUCT_HANDLE_TO_ITEM(future->result));
        }
        for (uint32_t i = 0; i < future->argc; i++)
        {
            if (REDUCT_HANDLE_IS_ITEM(future->argv[i]))
            {
                reduct_item_mark(REDUCT_HANDLE_TO_ITEM(future->argv[i]));
            }
        }
    }
    break;
    }
}