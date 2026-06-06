#include <reduct/core.h>
#include <reduct/error.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>
#include <reduct/task.h>

#include <string.h>

typedef struct
{
    struct reduct* master;
    uint64_t index;
} reduct_worker_ctx_t;

static bool reduct_task_push(reduct_task_env_t* env, void (*func)(struct reduct*, void*), void* arg, size_t* outHead)
{
    size_t head;
    reduct_task_t* slot;

    while (true)
    {
        head = atomic_load_explicit(&env->queueHead, memory_order_relaxed);
        slot = &env->queue[head % REDUCT_TASK_QUEUE_MAX];

        uint16_t seq = atomic_load_explicit(&slot->generation, memory_order_acquire);
        int16_t diff = (int16_t)(seq - (uint16_t)head);

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&env->queueHead, &head, head + 1, memory_order_relaxed,
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

static reduct_task_t* reduct_task_pop(reduct_t* reduct, reduct_task_env_t* env, size_t* outTail)
{
    size_t tail;
    reduct_task_t* slot;
    while (true)
    {
        tail = atomic_load_explicit(&env->queueTail, memory_order_relaxed);
        slot = &env->queue[tail % REDUCT_TASK_QUEUE_MAX];

        uint16_t seq = atomic_load_explicit(&slot->generation, memory_order_acquire);
        int16_t diff = (int16_t)(seq - (uint16_t)(tail + 1));

        if (diff == 0)
        {
            if (atomic_compare_exchange_weak_explicit(&env->queueTail, &tail, tail + 1, memory_order_relaxed,
                    memory_order_relaxed))
            {
                break;
            }
        }
        else if (diff < 0)
        {
            return NULL;
        }

        REDUCT_GC_CHECK(reduct);
    }

    *outTail = tail;
    return slot;
}

static bool reduct_task_done(reduct_task_env_t* env, reduct_task_id_t id)
{
    return atomic_load_explicit(&env->queue[id.index].generation, memory_order_acquire) != id.generation;
}

static bool reduct_task_try_work(reduct_t* reduct)
{
    reduct_task_env_t* env = &reduct->env->task;
    size_t tail;
    reduct_task_t* slot = reduct_task_pop(reduct, env, &tail);
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

static int reduct_task_worker(void* arg)
{
    reduct_worker_ctx_t* ctx = (reduct_worker_ctx_t*)arg;
    reduct_t* master = (reduct_t*)ctx->master;
    size_t index = ctx->index;
    free(ctx);

    reduct_task_env_t* env = &master->env->task;
    reduct_error_t err = REDUCT_ERROR();
    reduct_t* thread = reduct_new_thread(master, &err);

    if (thread == NULL)
    {
        return thrd_error;
    }

    thread->env->task.threads[index].reduct = thread;
    thread->env->task.threads[index].active = true;

    while (true)
    {
        for (size_t i = 0; i < REDUCT_TASK_SPIN_MAX; i++)
        {
            REDUCT_GC_CHECK(thread);

            if (reduct_task_try_work(thread))
            {
                i = 0;
                continue;
            }
            if (atomic_load_explicit(&env->shutdown, memory_order_relaxed))
            {
                break;
            }
        }

        if (atomic_load_explicit(&env->shutdown, memory_order_acquire))
        {
            break;
        }

        mtx_lock(&env->mutex);
        size_t head = atomic_load_explicit(&env->queueHead, memory_order_acquire);
        size_t tail = atomic_load_explicit(&env->queueTail, memory_order_acquire);

        if (head == tail && !atomic_load_explicit(&env->shutdown, memory_order_relaxed))
        {
            atomic_fetch_add_explicit(&env->idleCount, 1, memory_order_release);
            cnd_wait(&env->cond, &env->mutex);
            atomic_fetch_sub_explicit(&env->idleCount, 1, memory_order_release);
        }
        mtx_unlock(&env->mutex);

        REDUCT_GC_CHECK(thread);
    }

    env->threads[index].active = false;
    env->threads[index].reduct = NULL;
    reduct_free(thread);
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

REDUCT_API void reduct_task_env_init(reduct_task_env_t* env)
{
    assert(env != NULL);

    mtx_init(&env->mutex, mtx_plain);
    cnd_init(&env->cond);

    atomic_store(&env->queueHead, 0);
    atomic_store(&env->queueTail, 0);
    atomic_store(&env->threadCount, 0);
    atomic_store(&env->idleCount, 0);
    atomic_store(&env->barrierWaiters, 0);
    atomic_store(&env->shutdown, false);
    env->cpuCount = reduct_task_get_cpu_count();

    for (size_t i = 0; i < REDUCT_TASK_THREAD_MAX; i++)
    {
        env->threads[i].active = false;
    }

    for (size_t i = 0; i < REDUCT_TASK_QUEUE_MAX; i++)
    {
        atomic_store(&env->queue[i].generation, (uint32_t)i);
        env->queue[i].func = NULL;
        env->queue[i].arg = NULL;
    }
}

REDUCT_API void reduct_task_env_deinit(reduct_task_env_t* env)
{
    mtx_lock(&env->mutex);
    atomic_store_explicit(&env->shutdown, true, memory_order_release);
    cnd_broadcast(&env->cond);
    mtx_unlock(&env->mutex);

    size_t count = atomic_load(&env->threadCount);
    for (size_t i = 0; i < count; i++)
    {
        if (env->threads[i].active)
        {
            thrd_join(env->threads[i].thrd, NULL);
        }
    }
}

REDUCT_API bool reduct_task_create_try(reduct_t* reduct, void (*func)(struct reduct* reduct, void* arg), void* arg,
    reduct_task_id_t* outId)
{
    reduct_task_env_t* env = &reduct->env->task;
    if (REDUCT_UNLIKELY(atomic_load_explicit(&env->threadCount, memory_order_acquire) == 0))
    {
        mtx_lock(&env->mutex);

        if (atomic_load_explicit(&env->threadCount, memory_order_relaxed) == 0)
        {
            size_t limit = REDUCT_MIN(env->cpuCount, REDUCT_TASK_THREAD_MAX);

            env->threads[0].thrd = thrd_current();
            env->threads[0].active = true;
            env->threads[0].reduct = reduct;

            atomic_store_explicit(&env->threadCount, 1, memory_order_release);

            for (size_t i = 1; i < limit; i++)
            {
                reduct_worker_ctx_t* ctx = (reduct_worker_ctx_t*)malloc(sizeof(reduct_worker_ctx_t));
                if (ctx == NULL)
                {
                    break;
                }
                ctx->master = reduct;
                ctx->index = i;

                if (thrd_create(&env->threads[i].thrd, reduct_task_worker, ctx) != thrd_success)
                {
                    free(ctx);
                    mtx_unlock(&env->mutex);
                    REDUCT_ERROR_THROW(reduct, "failed to create thread");
                }
                atomic_fetch_add_explicit(&env->threadCount, 1, memory_order_release);
            }
        }

        mtx_unlock(&env->mutex);
    }

    size_t head;
    if (!reduct_task_push(env, func, arg, &head))
    {
        return false;
    }

    if (outId != NULL)
    {
        outId->index = (uint16_t)(head % REDUCT_TASK_QUEUE_MAX);
        outId->generation = (uint16_t)(head + 1);
    }

    cnd_signal(&env->cond);

    return true;
}

REDUCT_API reduct_task_id_t reduct_task_create(reduct_t* reduct, void (*func)(struct reduct* reduct, void* arg),
    void* arg)
{
    reduct_task_id_t id;

    while (!reduct_task_create_try(reduct, func, arg, &id))
    {
        if (!reduct_task_try_work(reduct))
        {
            REDUCT_GC_CHECK(reduct);
            thrd_yield();
        }
    }

    return id;
}

REDUCT_API void reduct_task_join(reduct_t* reduct, reduct_task_id_t id)
{
    while (!reduct_task_done(&reduct->env->task, id))
    {
        if (!reduct_task_try_work(reduct))
        {
            REDUCT_GC_CHECK(reduct);
            thrd_yield();
        }
    }
}

REDUCT_API void reduct_task_barrier_enter(reduct_t* reduct)
{
    reduct_task_env_t* env = &reduct->env->task;

    atomic_fetch_add_explicit(&env->barrierWaiters, 1, memory_order_release);

    while (true)
    {
        uint32_t waiters = atomic_load_explicit(&env->barrierWaiters, memory_order_acquire);
        uint32_t idle = atomic_load_explicit(&env->idleCount, memory_order_acquire);
        uint32_t total = atomic_load_explicit(&env->threadCount, memory_order_acquire);

        if (waiters + idle >= total)
        {
            break;
        }

        thrd_yield();
    }
}

REDUCT_API void reduct_task_barrier_exit(reduct_t* reduct)
{
    reduct_task_env_t* env = &reduct->env->task;

    atomic_fetch_sub_explicit(&env->barrierWaiters, 1, memory_order_release);
}