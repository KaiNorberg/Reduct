#include "reduct/error.h"
#ifndef REDUCT_FUTURE_H
#define REDUCT_FUTURE_H 1

#include <reduct/defs.h>
#include <reduct/task.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <threads.h>

struct reduct;

/**
 * @file future.h
 * @brief Future primitives.
 * @defgroup future Future
 *
 * @{
 */

#define REDUCT_FUTURE_SMALL_MAX 2 ///< The maximum number of small arguments in a future.

/**
 * @brief Future structure.
 * @struct reduct_future_t
 */
typedef struct reduct_future
{
    reduct_error_t* error; ///< Error structure, will be `NULL` if no error occured.
    union {
        reduct_handle_t callable; ///< The callable to execute.
        reduct_handle_t result;   ///< The result of the execution.
    };
    reduct_handle_t smallArgv[REDUCT_FUTURE_SMALL_MAX];
    reduct_handle_t* argv;   ///< The arguments for the callable.
    uint32_t argc;           ///< The number of arguments.
    _Atomic(bool) done;      ///< Whether the future is finished.
    reduct_task_id_t taskId; ///< The task ID in the task system.
} reduct_future_t;

/**
 * @brief Create a new future and start its execution.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param callable The callable handle.
 * @param argc The number of arguments.
 * @param argv Pointer to the arguments array.
 * @return Pointer to the new future.
 */
REDUCT_API reduct_future_t* reduct_future_new(struct reduct* reduct, reduct_handle_t callable, size_t argc,
    reduct_handle_t* argv);

/**
 * @brief Wait for the future to complete and return its result.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param future Pointer to the future.
 * @return The result handle.
 */
REDUCT_API reduct_handle_t reduct_future_join(struct reduct* reduct, reduct_future_t* future);

/**
 * @brief Check if the future is finished.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param future Pointer to the future.
 * @return `true` if done, `false` otherwise.
 */
REDUCT_API bool reduct_future_is_done(struct reduct* reduct, reduct_future_t* future);

/** @} */

#endif
