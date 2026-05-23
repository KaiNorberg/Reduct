#include "reduct/gc.h"
#include "reduct/atom.h"
#include "reduct/core.h"
#include "reduct/eval.h"
#include "reduct/item.h"
#include "reduct/list.h"

static void reduct_gc_mark(reduct_t* reduct, reduct_item_t* item);

static void reduct_gc_mark_node(reduct_t* reduct, uint32_t shift, reduct_list_node_t* node);

static void reduct_gc_mark_node_contents(reduct_t* reduct, uint32_t shift, reduct_list_node_t* node)
{
    if (shift == 0)
    {
        for (uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_handle_t handle = node->handles[i];
            if (REDUCT_HANDLE_IS_ITEM(handle))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(handle));
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
        {
            reduct_gc_mark_node(reduct, shift - REDUCT_LIST_BITS, node->children[i]);
        }
    }
}

static void reduct_gc_mark_node(reduct_t* reduct, uint32_t shift, reduct_list_node_t* node)
{
    if (node == NULL)
    {
        return;
    }

    reduct_item_t* item = REDUCT_CONTAINER_OF(node, reduct_item_t, listNode);
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
    if (atom->flags & REDUCT_ATOM_FLAG_LARGE && atom->stack != NULL)
    {
        reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(atom->stack, reduct_item_t, atomStack));
    }
}

static void reduct_gc_mark(reduct_t* reduct, reduct_item_t* item)
{
    assert(reduct != NULL);

    if (REDUCT_UNLIKELY(item == NULL || (item->flags & REDUCT_ITEM_FLAG_GC_MARK)))
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
        for (uint16_t i = 0; i < item->function.constantCount; i++)
        {
            if (item->function.constants[i].type == REDUCT_CONST_SLOT_TYPE_HANDLE)
            {
                if (REDUCT_HANDLE_IS_ITEM(item->function.constants[i].handle))
                {
                    reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(item->function.constants[i].handle));
                }
            }
            else if (item->function.constants[i].type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
            {
                reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(item->function.constants[i].capture, reduct_item_t, atom));
            }
        }
    }
    else if (item->type == REDUCT_ITEM_TYPE_CLOSURE)
    {
        reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(item->closure.function, reduct_item_t, function));
        for (uint16_t i = 0; i < item->closure.function->constantCount; i++)
        {
            reduct_handle_t handle = item->closure.constants[i];
            if (REDUCT_HANDLE_IS_ITEM(handle))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(handle));
            }
        }
    }
}

REDUCT_API void reduct_gc(reduct_t* reduct)
{
    assert(reduct != NULL);

    reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(reduct->nil));

    for (size_t i = 0; i < reduct->retainedCount; i++)
    {
        reduct_gc_mark(reduct, reduct->retained[i]);
    }

    if (reduct != NULL)
    {
        for (uint32_t i = 0; i < reduct->regCount; i++)
        {
            reduct_handle_t child = reduct->regs[i];
            if (REDUCT_HANDLE_IS_ITEM(child))
            {
                reduct_gc_mark(reduct, REDUCT_HANDLE_TO_ITEM(child));
            }
        }
        for (uint32_t i = 0; i < reduct->frameCount; i++)
        {
            reduct_closure_t* closure = reduct->frames[i].closure;
            reduct_gc_mark(reduct, REDUCT_CONTAINER_OF(closure, reduct_item_t, closure));
        }
    }

    reduct->freeList = NULL;
    reduct->freeCount = 0;

    reduct_item_block_t* prev = NULL;
    reduct_item_block_t* block = reduct->block;
    while (block != NULL)
    {
        reduct_item_block_t* next = block->next;
        uint32_t count = 0;
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            if (block->items[i].flags & REDUCT_ITEM_FLAG_GC_MARK)
            {
                count++;
            }
        }

        if (count == 0)
        {
            for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
            {
                reduct_item_deinit(reduct, &block->items[i]);
            }
            reduct->blockCount--;
            free(block->allocated);
        }
        else
        {
            for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
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

            if (prev == NULL)
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

    if (prev != NULL)
    {
        prev->next = NULL;
    }
    else
    {
        reduct->block = NULL;
    }
    reduct->prevBlockCount = reduct->blockCount;
}

REDUCT_API void reduct_gc_retain(reduct_t* reduct, reduct_item_t* item)
{
    assert(reduct != NULL);
    assert(item != NULL);

    for (size_t i = 0; i < reduct->retainedCount; i++)
    {
        if (reduct->retained[i] == item)
        {
            return;
        }
    }

    if (reduct->retainedCount >= reduct->retainedCapacity)
    {
        size_t newCapacity = reduct->retainedCapacity == 0 ? REDUCT_GC_RETAINED_INITAL
                                                           : reduct->retainedCapacity * REDUCT_GC_RETAINED_GROWTH;
        reduct_item_t** newRetained = (reduct_item_t**)realloc(reduct->retained, newCapacity * sizeof(reduct_item_t*));
        if (newRetained == NULL)
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
    assert(reduct != NULL);
    assert(item != NULL);

    for (size_t i = 0; i < reduct->retainedCount; i++)
    {
        if (reduct->retained[i] == item)
        {
            reduct->retained[i] = reduct->retained[--reduct->retainedCount];
            return;
        }
    }
}
