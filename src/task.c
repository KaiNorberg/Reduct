#include <reduct/core.h>
#include <reduct/error.h>
#include <reduct/eval.h>
#include <reduct/item.h>
#include <reduct/task.h>

#include <string.h>

static bool reduct_task_push(reduct_task_global_t* global, void (*func)(struct reduct*, void*), void* arg,
    size_t* outHead)
{
    size_t head;
    reduct_task_t* slot;

    while (true)
    {
        head = atomic_load_explicit(&global->queueHead, memory_order_relaxed);
        slot = &global->queue[head % REDUCT_TASK_QUEUE_MAX];

        uint16_t seq = atomic_load_explicit(&slot->generation, memory_order_acquire);
        int16_t diff = (int16_t)(seq - (uint16_t)head);

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&global->queueHead, &head, head + 1, memory_order_relaxed,
                    memory_order_relaxed))
            {
                break;
            }
        }
        else if (diff < 0)
        {
            return false;
        }
    }

    slot->func = func;
    slot->arg = arg;

    if (outHead != NULL)
    {
        *outHead = head;
    }

    atomic_store_explicit(&slot->generation, (uint32_t)(head + 1), memory_order_release);
    return true;
}

static reduct_task_t* reduct_task_pop(reduct_t* reduct, reduct_task_global_t* global, size_t* outTail)
{
    size_t tail;
    reduct_task_t* slot;
    while (true)
    {
        tail = atomic_load_explicit(&global->queueTail, memory_order_relaxed);
        slot = &global->queue[tail % REDUCT_TASK_QUEUE_MAX];

        uint16_t seq = atomic_load_explicit(&slot->generation, memory_order_acquire);
        int16_t diff = (int16_t)(seq - (uint16_t)(tail + 1));

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&global->queueTail, &tail, tail + 1, memory_order_relaxed,
                    memory_order_relaxed))
            {
                break;
            }
        }
        else if (diff < 0)
        {
            return NULL;
        }
    }

    *outTail = tail;
    return slot;
}

static bool reduct_task_done(reduct_task_global_t* global, reduct_task_id_t id)
{
    return atomic_load_explicit(&global->queue[id.index].generation, memory_order_acquire) != id.generation;
}

static bool reduct_task_try_work(reduct_t* reduct)
{
    reduct_task_global_t* global = &reduct->global->task;
    size_t tail;
    reduct_task_t* slot = reduct_task_pop(reduct, global, &tail);
    if (slot == NULL)
    {
        return false;
    }

    void (*func)(struct reduct*, void*) = slot->func;
    void* arg = slot->arg;
    func(reduct, arg);

    atomic_store_explicit(&slot->generation, (uint16_t)(tail + REDUCT_TASK_QUEUE_MAX), memory_order_release);

    return true;
}

REDUCT_API int reduct_task_worker(void* arg)
{
    reduct_t* thread = (reduct_t*)arg;
    reduct_task_global_t* global = &thread->global->task;

    while (true)
    {
        for (size_t i = 0; i < REDUCT_TASK_SPIN_MAX; i++)
        {
            if (reduct_task_try_work(thread))
            {
                i = 0;
                continue;
            }
            if (atomic_load_explicit(&global->shutdown, memory_order_relaxed))
            {
                break;
            }
        }

        if (atomic_load_explicit(&global->shutdown, memory_order_acquire))
        {
            break;
        }

        mtx_lock(&global->mutex);
        size_t head = atomic_load_explicit(&global->queueHead, memory_order_acquire);
        size_t tail = atomic_load_explicit(&global->queueTail, memory_order_acquire);

        if (head == tail && !atomic_load_explicit(&global->shutdown, memory_order_relaxed))
        {
            atomic_fetch_add_explicit(&global->idleCount, 1, memory_order_release);
            cnd_wait(&global->cond, &global->mutex);
            atomic_fetch_sub_explicit(&global->idleCount, 1, memory_order_release);
        }
        mtx_unlock(&global->mutex);

        REDUCT_GC_CHECK(thread);
    }

    return 0;
}

static size_t reduct_task_get_cpu_count(void)
{
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? (size_t)sysinfo.dwNumberOfProcessors : 1;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? (size_t)nprocs : 1;
#endif
}

REDUCT_API void reduct_task_global_init(reduct_task_global_t* global)
{
    assert(global != NULL);

    mtx_init(&global->mutex, mtx_plain);
    cnd_init(&global->cond);

    atomic_store(&global->queueHead, 0);
    atomic_store(&global->queueTail, 0);
    atomic_store(&global->idleCount, 0);
    atomic_store(&global->barrierCount, 0);
    atomic_store(&global->barrierGen, 0);
    atomic_store(&global->shutdown, false);

    for (size_t i = 0; i < REDUCT_TASK_QUEUE_MAX; i++)
    {
        atomic_store(&global->queue[i].generation, (uint32_t)i);
        global->queue[i].func = NULL;
        global->queue[i].arg = NULL;
    }
}

REDUCT_API void reduct_task_global_deinit(reduct_task_global_t* global)
{
    mtx_lock(&global->mutex);
    atomic_store_explicit(&global->shutdown, true, memory_order_release);
    cnd_broadcast(&global->cond);
    mtx_unlock(&global->mutex);
}

REDUCT_API bool reduct_task_create_try(reduct_t* reduct, void (*func)(struct reduct* reduct, void* arg), void* arg,
    reduct_task_id_t* outId)
{
    reduct_task_global_t* global = &reduct->global->task;

    size_t head;
    if (!reduct_task_push(global, func, arg, &head))
    {
        return false;
    }

    if (outId != NULL)
    {
        outId->index = (uint16_t)(head % REDUCT_TASK_QUEUE_MAX);
        outId->generation = (uint16_t)(head + 1);
    }

    cnd_signal(&global->cond);

    return true;
}

REDUCT_API reduct_task_id_t reduct_task_create(reduct_t* reduct, void (*func)(struct reduct* reduct, void* arg),
    void* arg)
{
    reduct_task_id_t id;

    while (!reduct_task_create_try(reduct, func, arg, &id))
    {
        reduct_task_try_work(reduct);
        REDUCT_GC_CHECK(reduct);
        thrd_yield();
    }

    return id;
}

REDUCT_API void reduct_task_join(reduct_t* reduct, reduct_task_id_t id)
{
    while (!reduct_task_done(&reduct->global->task, id))
    {
        reduct_task_try_work(reduct);
        REDUCT_GC_CHECK(reduct);
        thrd_yield();
    }
}

REDUCT_API void reduct_task_barrier(reduct_t* reduct)
{
    reduct_task_global_t* global = &reduct->global->task;

    uint32_t gen = atomic_load_explicit(&global->barrierGen, memory_order_acquire);

    if (atomic_fetch_add_explicit(&global->barrierCount, 1, memory_order_acq_rel) + 1 >= reduct->global->threadCount)
    {
        atomic_store_explicit(&global->barrierCount, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&global->barrierGen, 1, memory_order_release);
        cnd_broadcast(&global->cond);
        return;
    }

    while (atomic_load_explicit(&global->barrierGen, memory_order_acquire) == gen)
    {
        if (atomic_load_explicit(&global->shutdown, memory_order_relaxed))
        {
            break;
        }
        cnd_broadcast(&global->cond);
        thrd_yield();
    }
}
