#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/task.h>

REDUCT_API void reduct_gc_env_init(reduct_gc_env_t* env)
{
    atomic_init(&env->count, 0);
    env->completed = 0;
    mtx_init(&env->mutex, mtx_plain);
}

REDUCT_API void reduct_gc_env_deinit(reduct_gc_env_t* env)
{
    mtx_destroy(&env->mutex);
}

REDUCT_API void reduct_gc_state_init(reduct_gc_state_t* state)
{
    state->retained = NULL;
    state->retainedCount = 0;
    state->retainedCapacity = 0;
    state->lastCount = 0;
}

REDUCT_API void reduct_gc_state_deinit(reduct_gc_state_t* state)
{
    if (state->retained != NULL)
    {
        free(state->retained);
        state->retained = NULL;
    }
}

static inline void reduct_gc_mark_state(reduct_t* state)
{
    reduct_item_mark(REDUCT_HANDLE_TO_ITEM(state->nil));

    for (uint32_t j = 0; j < state->eval.regCount; j++)
    {
        if (REDUCT_HANDLE_IS_ITEM(state->eval.regs[j]))
        {
            reduct_item_mark(REDUCT_HANDLE_TO_ITEM(state->eval.regs[j]));
        }
    }
    for (uint32_t j = 0; j < state->eval.frameCount; j++)
    {
        reduct_item_mark(REDUCT_CONTAINER_OF(state->eval.frames[j].closure, reduct_item_t, closure));
    }

    state->item.freeList = NULL;
    state->item.freeCount = 0;
}

static inline void reduct_item_sweep(reduct_t* reduct)
{
    reduct_item_env_t* env = &reduct->env->item;
    reduct_item_block_t* prev = NULL;
    reduct_item_block_t* block = env->block;

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
            env->blockCount--;
            free(block->allocated);
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
                env->block = block;
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
        env->block = NULL;
    }

    env->globalFreeList = globalFreeList;
    env->globalFreeCount = globalFreeCount;
    env->prevBlockCount = env->blockCount;
}

REDUCT_API void reduct_gc(reduct_t* reduct)
{
    assert(reduct != NULL);
    reduct_gc_env_t* gc = &reduct->env->gc;

    reduct_task_barrier_enter(reduct);
    mtx_lock(&reduct->env->task.mutex);

    mtx_lock(&gc->mutex);
    uint32_t count = atomic_load(&gc->count);
    if (gc->completed < count)
    {
        reduct_gc_mark_state(reduct);

        for (size_t i = 0; i < reduct->gc.retainedCount; i++)
        {
            reduct_item_mark(reduct->gc.retained[i]);
        }

        for (size_t i = 0; i < reduct->env->task.threadCount; i++)
        {
            reduct_thread_t* thread = &reduct->env->task.threads[i];
            if (!thread->active || thread->reduct == NULL || thread->reduct == reduct)
            {
                continue;
            }

            reduct_gc_mark_state(thread->reduct);
            for (size_t j = 0; j < thread->reduct->gc.retainedCount; j++)
            {
                reduct_item_mark(thread->reduct->gc.retained[j]);
            }
        }

        reduct_item_sweep(reduct);
        gc->completed = count;
    }

    reduct->gc.lastCount = count;

    mtx_unlock(&gc->mutex);
    mtx_unlock(&reduct->env->task.mutex);
    reduct_task_barrier_exit(reduct);
}

REDUCT_API void reduct_gc_retain(reduct_t* reduct, reduct_item_t* item)
{
    reduct_gc_state_t* gc = &reduct->gc;

    for (size_t i = 0; i < gc->retainedCount; i++)
    {
        if (gc->retained[i] == item)
        {
            return;
        }
    }

    if (gc->retainedCount >= gc->retainedCapacity)
    {
        size_t newCapacity =
            gc->retainedCapacity == 0 ? REDUCT_GC_RETAINED_INITIAL : gc->retainedCapacity * REDUCT_GC_RETAINED_GROWTH;
        reduct_item_t** newRetained = (reduct_item_t**)realloc(gc->retained, newCapacity * sizeof(reduct_item_t*));
        if (newRetained == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        gc->retained = newRetained;
        gc->retainedCapacity = newCapacity;
    }

    gc->retained[gc->retainedCount++] = item;
}

REDUCT_API void reduct_gc_release(reduct_t* reduct, reduct_item_t* item)
{
    reduct_gc_state_t* gc = &reduct->gc;

    for (size_t i = 0; i < gc->retainedCount; i++)
    {
        if (gc->retained[i] == item)
        {
            gc->retained[i] = gc->retained[--gc->retainedCount];
            return;
        }
    }
}