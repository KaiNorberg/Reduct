#include "reduct/atom.h"
#include "reduct/core.h"
#include "reduct/eval.h"
#include "reduct/gc.h"
#include "reduct/item.h"
#include "reduct/list.h"

static void reduct_gc_mark(reduct_t* reduct, reduct_item_t* item);

static void reduct_gc_mark_node(reduct_t* reduct, reduct_uint32_t shift, reduct_list_node_t* node);

static void reduct_gc_mark_node_contents(reduct_t* reduct, reduct_uint32_t shift, reduct_list_node_t* node)
{
    if (shift == 0)
    {
        for (reduct_uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_handle_t h = node->handles[i];
            if (REDUCT_HANDLE_IS_ITEM(&h))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(&h));
            }
        }
    }
    else
    {
        for (reduct_uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_gc_mark_node(reduct, shift - REDUCT_LIST_BITS, node->children[i]);
        }
    }
}

static void reduct_gc_mark_node(reduct_t* reduct, reduct_uint32_t shift, reduct_list_node_t* node)
{
    if (node == REDUCT_NULL)
    {
        return;
    }

    reduct_item_t* item = REDUCT_CONTAINER_OF(node, reduct_item_t, node);
    if (item->flags & REDUCT_ITEM_FLAG_GC_MARK)
    {
        return;
    }
    item->flags |= REDUCT_ITEM_FLAG_GC_MARK;

    reduct_gc_mark_node_contents(reduct, shift, node);
}

static void reduct_gc_mark_list(reduct_t* reduct, reduct_list_t* list)
{
    reduct_gc_mark_node(reduct, list->shift, list->root);
    reduct_gc_mark_node_contents(reduct, 0, &list->tail);
}

static void reduct_gc_mark_atom(reduct_t* reduct, reduct_atom_t* atom)
{
    if (atom->flags & REDUCT_ATOM_FLAG_LARGE && atom->stack != REDUCT_NULL)
    {
        reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(atom->stack, reduct_item_t, atomStack));
    }
}

static void reduct_gc_mark(reduct_t* reduct, reduct_item_t* item)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (REDUCT_UNLIKELY(item == REDUCT_NULL || (item->flags & REDUCT_ITEM_FLAG_GC_MARK)))
    {
        return;
    }

    item->flags |= REDUCT_ITEM_FLAG_GC_MARK;

    if (item->type == REDUCT_ITEM_TYPE_LIST)
    {
        reduct_gc_mark_list(reduct, &item->list);
    }
    else if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        reduct_gc_mark_atom(reduct, &item->atom);
    }
    else if (item->type == REDUCT_ITEM_TYPE_FUNCTION)
    {
        for (reduct_uint16_t i = 0; i < item->function.constantCount; i++)
        {
            if (item->function.constants[i].type == REDUCT_CONST_SLOT_ITEM)
            {
                reduct_gc_mark(reduct, item->function.constants[i].item);
            }
            else if (item->function.constants[i].type == REDUCT_CONST_SLOT_CAPTURE)
            {
                reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(item->function.constants[i].capture, reduct_item_t, atom));
            }
        }
    }
    else if (item->type == REDUCT_ITEM_TYPE_CLOSURE)
    {
        reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(item->closure.function, reduct_item_t, function));
        for (reduct_uint16_t i = 0; i < item->closure.function->constantCount; i++)
        {
            reduct_handle_t handle = item->closure.constants[i];
            if (REDUCT_HANDLE_IS_ITEM(&handle))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(&handle));
            }
        }
    }
}

REDUCT_API void reduct_gc(reduct_t* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    for (reduct_size_t i = 0; i < reduct->retainedCount; i++)
    {
        reduct_gc_mark(reduct, reduct->retained[i]);
    }

    for (reduct_uint32_t i = 0; i < reduct->constantCount; i++)
    {
        reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(reduct->constants[i].name, reduct_item_t, atom));
        reduct_gc_mark(reduct, reduct->constants[i].item);
    }

    if (reduct != REDUCT_NULL)
    {
        for (reduct_uint32_t i = 0; i < reduct->regCount; i++)
        {
            reduct_handle_t child = reduct->regs[i];
            if (REDUCT_HANDLE_IS_ITEM(&child))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(&child));
            }
        }
        for (reduct_uint32_t i = 0; i < reduct->frameCount; i++)
        {
            reduct_closure_t* closure = reduct->frames[i].closure;
            reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(closure, reduct_item_t, closure));
        }
    }

    reduct->freeList = REDUCT_NULL;
    reduct->freeCount = 0;

    reduct_item_block_t* prev = REDUCT_NULL;
    reduct_item_block_t* block = reduct->block;
    while (block != REDUCT_NULL)
    {
        reduct_item_block_t* next = block->next;
        reduct_uint32_t count = 0;
        for (reduct_uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            if (block->items[i].flags & REDUCT_ITEM_FLAG_GC_MARK)
            {
                count++;
            }
        }

        if (count == 0)
        {
            for (reduct_uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
            {
                reduct_item_deinit(reduct, &block->items[i]);
            }
            reduct->blockCount--;
            REDUCT_FREE(block->allocated);
        }
        else
        {
            for (reduct_uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
            {
                reduct_item_t* item = &block->items[i];
                if (item->flags & REDUCT_ITEM_FLAG_GC_MARK)
                {
                    item->flags &= ~REDUCT_ITEM_FLAG_GC_MARK;
                }
                else
                {
                    reduct_item_free(reduct, item);
                }
            }

            if (prev == REDUCT_NULL)
            {
                reduct->block = block;
            }
            else
            {
                prev->next = block;
            }
            prev = block;
        }
        block = next;
    }

    if (prev != REDUCT_NULL)
    {
        prev->next = REDUCT_NULL;
    }
    else
    {
        reduct->block = REDUCT_NULL;
    }
    reduct->prevBlockCount = reduct->blockCount;
}

REDUCT_API void reduct_gc_retain(reduct_t* reduct, reduct_item_t* item)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(item != REDUCT_NULL);

    if (reduct->retainedCount >= reduct->retainedCapacity)
    {
        reduct_size_t newCapacity = reduct->retainedCapacity == 0 ? REDUCT_GC_RETAINED_INITAL : reduct->retainedCapacity * REDUCT_GC_RETAINED_GROWTH;
        reduct_item_t** newRetained = (reduct_item_t**)REDUCT_REALLOC(reduct->retained, newCapacity * sizeof(reduct_item_t*));
        if (newRetained == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->retained = newRetained;
        reduct->retainedCapacity = newCapacity;
    }

    reduct->retained[reduct->retainedCount++] = item;
}

REDUCT_API void reduct_gc_release(reduct_t* reduct, reduct_item_t* item)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(item != REDUCT_NULL);

    for (reduct_size_t i = 0; i < reduct->retainedCount; i++)
    {
        if (reduct->retained[i] == item)
        {
            reduct->retained[i] = reduct->retained[--reduct->retainedCount];
            return;
        }
    }
}