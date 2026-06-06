#ifndef REDUCT_TASK_H
#define REDUCT_TASK_H 1

#include <reduct/defs.h>
#include <reduct/error.h>
#include <reduct/sync.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

struct reduct_task;

/**
 * @file task.h
 * @brief Task primitives.
 * @defgroup task Task
 *
 * @{
 */

#define REDUCT_TASK_THREAD_MAX 16 ///< The maximum number of threads dedicated to tasks.

#define REDUCT_TASK_QUEUE_MAX 256 ///< The maximum number of tasks.

#define REDUCT_TASK_SPIN_MAX 1000 ///< The maximum number of times to spin while waiting for work to be ready.

/**
 * @brief Thread structure.
 * @struct reduct_thread_t
 */
typedef struct
{
    struct reduct* reduct;
    thrd_t thrd;
    bool active;
} reduct_thread_t;
/**
 * @brief Task ID structure.
 * @struct reduct_task_id_t
 */
typedef struct
{
    uint16_t index; ///< The index within the queue.
    uint16_t
        generation; ///< Incrementally increasing counter to avoid ABA problems when reusing task slots in the queue.
} reduct_task_id_t;

/**
 * @brief Task structure.
 * @struct reduct_task_t
 */
typedef struct REDUCT_ALIGNED(64) reduct_task
{
    _Atomic(uint16_t) generation;
    void (*func)(struct reduct* reduct, void* arg);
    void* arg;
} reduct_task_t;

/**
 * @brief Global thread environment structure.
 * @struct reduct_task_env_t
 */
typedef struct
{
    mtx_t mutex;
    cnd_t cond;
    reduct_thread_t threads[REDUCT_TASK_THREAD_MAX];
    _Atomic(bool) shutdown;
    REDUCT_ALIGNED(64) reduct_task_t queue[REDUCT_TASK_QUEUE_MAX];
    REDUCT_ALIGNED(64) _Atomic(size_t) queueHead;
    REDUCT_ALIGNED(64) _Atomic(size_t) queueTail;
    uint32_t cpuCount;
    _Atomic(uint32_t) threadCount;
    _Atomic(uint32_t) idleCount;
    _Atomic(uint32_t) barrierWaiters;
} reduct_task_env_t;

/**
 * @brief Initialize an thread environment.
 *
 * @param env Pointer to the thread environment to initialize.
 */
REDUCT_API void reduct_task_env_init(reduct_task_env_t* env);

/**
 * @brief Deinitialize an thread environment.
 *
 * @param env Pointer to the thread environment to deinitialize.
 */
REDUCT_API void reduct_task_env_deinit(reduct_task_env_t* env);

/**
 * @brief Create a new task.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function to execute in the task.
 * @param arg The argument to pass to the function.
 * @return The ID of the created task.
 */
REDUCT_API reduct_task_id_t reduct_task_create(struct reduct* reduct, void (*func)(struct reduct* reduct, void* arg),
    void* arg);

/**
 * @brief Try to create a new task without blocking.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function to execute in the task.
 * @param arg The argument to pass to the function.
 * @param outId Pointer to store the ID of the created task.
 * @return `true` if the task was created, `false` if the queue is full.
 */
REDUCT_API bool reduct_task_create_try(struct reduct* reduct, void (*func)(struct reduct* reduct, void* arg), void* arg,
    reduct_task_id_t* outId);

/**
 * @brief Wait for a task to finish.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The id of the task to wait for.
 */
REDUCT_API void reduct_task_join(struct reduct* reduct, reduct_task_id_t id);

/**
 * @brief Retrieve the number of pending tasks.
 *
 * @param env Pointer to the thread environment.
 * @return The number of pending tasks in the queue.
 */
static inline REDUCT_ALWAYS_INLINE uint64_t reduct_task_queue_size(reduct_task_env_t* env)
{
    size_t head = atomic_load_explicit(&env->queueHead, memory_order_acquire);
    size_t tail = atomic_load_explicit(&env->queueTail, memory_order_acquire);
    return head - tail;
}

/**
 * @brief Barrier function to ensure all threads have either reached the barrier or are idle.
 *
 * Once the function returns all threads that have reached the barrier will be blocked until the barrier is exited, and
 * all threads that have not reached the barrier will be idle until the barrier is exited.
 *
 * @param reduct Pointer to the Reduct structure.
 */
REDUCT_API void reduct_task_barrier_enter(struct reduct* reduct);

/**
 * @brief Exit the barrier, allowing threads to resume execution.
 *
 * @param reduct Pointer to the Reduct structure.
 */
REDUCT_API void reduct_task_barrier_exit(struct reduct* reduct);

/** @} */

#endif
