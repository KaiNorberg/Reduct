#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/task.h>
#include <threads.h>

REDUCT_API void reduct_gc_global_init(reduct_gc_global_t* global)
{
    atomic_init(&global->requested, false);
}

REDUCT_API void reduct_gc_global_deinit(reduct_gc_global_t* global)
{
    REDUCT_UNUSED(global);
}

static inline void reduct_item_sweep(reduct_t* reduct)
{
    reduct_item_global_t* global = &reduct->global->item;
    mtx_lock(&global->mutex);

    reduct_item_block_t* prev = NULL;
    reduct_item_block_t* block = global->block;

    reduct_item_block_t* blocksToFree = NULL;
    reduct_item_t* globalFreeList = NULL;
    size_t globalFreeCount = 0;

    while (block != NULL)
    {
        reduct_item_block_t* next = block->next;
        uint32_t count = 0;
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            if (atomic_load(&block->items[i].flags) & REDUCT_ITEM_FLAG_MARKED)
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
            global->blockCount--;

            block->next = blocksToFree;
            blocksToFree = block;
        }
        else
        {
            for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
            {
                reduct_item_t* item = &block->items[i];
                if (atomic_load(&block->items[i].flags) & REDUCT_ITEM_FLAG_MARKED)
                {
                    atomic_fetch_and(&item->flags, ~REDUCT_ITEM_FLAG_MARKED);
                }
                else
                {
                    if (item->type != REDUCT_ITEM_TYPE_NONE)
                    {
                        reduct_item_deinit(reduct, item);
                    }
                    item->free = globalFreeList;
                    globalFreeList = item;
                    globalFreeCount++;
                }
            }

            if (prev == NULL)
            {
                global->block = block;
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
        global->block = NULL;
    }

    global->globalFreeList = globalFreeList;
    global->globalFreeCount = globalFreeCount;
    global->prevBlockCount = global->blockCount;

    mtx_unlock(&global->mutex);

    while (blocksToFree != NULL)
    {
        reduct_item_block_t* next = blocksToFree->next;
        free(blocksToFree->allocated);
        blocksToFree = next;
    }
}

REDUCT_API void reduct_gc(reduct_t* reduct)
{
    assert(reduct != NULL);
    reduct_gc_global_t* gc = &reduct->global->gc;

    atomic_store_explicit(&gc->requested, true, memory_order_relaxed);
    reduct_task_barrier(reduct);

    for (uint32_t j = 0; j < reduct->eval.regCount; j++)
    {
        reduct_handle_t handle = reduct->eval.regs[j];
        if (REDUCT_HANDLE_IS_ITEM(handle))
        {
            reduct_item_mark(REDUCT_HANDLE_TO_ITEM(handle));
        }
    }
    for (uint32_t j = 0; j < reduct->eval.frameCount; j++)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(reduct->eval.frames[j].closure, reduct_item_t, closure));
    }

    reduct->item.freeList = NULL;
    reduct->item.freeCount = 0;

    reduct_task_barrier(reduct);

    if (atomic_exchange(&gc->requested, false))
    {
        reduct_item_mark(REDUCT_HANDLE_TO_ITEM(reduct->global->nil));

        for (size_t i = 0; i < reduct->global->threadCount; i++)
        {
            reduct_t* thread = &reduct->global->threads[i];

            for (uint32_t j = 0; j < thread->eval.regCount; j++)
            {
                reduct_handle_t handle = thread->eval.regs[j];
                if (REDUCT_HANDLE_IS_ITEM(handle))
                {
                    reduct_item_mark(REDUCT_HANDLE_TO_ITEM(handle));
                }
            }
            for (uint32_t j = 0; j < thread->eval.frameCount; j++)
            {
                reduct_item_mark(REDUCT_CONTAINER_OF(thread->eval.frames[j].closure, reduct_item_t, closure));
            }

            thread->item.freeList = NULL;
            thread->item.freeCount = 0;
        }

        reduct_item_global_t* itemEnv = &reduct->global->item;
        mtx_lock(&itemEnv->mutex);
        reduct_item_block_t* block = itemEnv->block;
        while (block != NULL)
        {
            for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
            {
                reduct_item_t* item = &block->items[i];
                if (atomic_load(&item->flags) & REDUCT_ITEM_FLAG_RETAINED)
                {
                    reduct_item_mark(item);
                }
            }
            block = block->next;
        }
        mtx_unlock(&itemEnv->mutex);

        reduct_item_sweep(reduct);
    }

    reduct_task_barrier(reduct);
}
