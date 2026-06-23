#ifndef REDUCT_CORE_H
#define REDUCT_CORE_H 1

#include <reduct/arena.h>
#include <reduct/atom.h>
#include <reduct/defs.h>
#include <reduct/error.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/module.h>
#include <reduct/native.h>
#include <reduct/schema.h>
#include <reduct/scratch.h>
#include <reduct/task.h>

struct reduct_item;
struct reduct_eval_frame;

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>

/**
 * @file core.h
 * @brief Core definitions and structures.
 * @defgroup core
 *
 * @{
 */

#define REDUCT_SCHEMA_INITIAL 4 ///< Initial size of the schema array.
#define REDUCT_SCHEMA_GROWTH 2  ///< Growth factor of the schema array.

#define REDUCT_CONSTANTS_MAX 8 ///< Maximum amount of predefined constants.

#define REDUCT_SCRATCH_INITIAL 128 ///< Initial scratch buffer size.
#define REDUCT_SCRATCH_MAX 16      ///< The maximum number of scratch buffers.

/**
 * @brief Global state structure.
 * @struct reduct_global_t
 */
typedef struct reduct_global
{
    int argc;
    char** argv;
    reduct_handle_t nil;
    reduct_module_global_t module;
    struct reduct* threads;
    uint64_t threadCount;
    reduct_atom_global_t atom;
    reduct_native_global_t native;
    reduct_item_global_t item;
    reduct_gc_global_t gc;
    reduct_schema_global_t schema;
    reduct_optimize_global_t optimize;
    reduct_task_global_t task;
} reduct_global_t;

/**
 * @brief Per-thread state structure.
 * @struct reduct_t
 */
typedef struct reduct
{
    thrd_t thrd;
    reduct_global_t* global;
    reduct_error_t* error;
    reduct_arena_local_t arena;
    reduct_item_local_t item;
    reduct_scratch_local_t scratch;
    reduct_eval_local_t eval;
    void* userdata;
} reduct_t;

/**
 * @brief Create a new Reduct environment.
 *
 * @return A pointer to the first per thread state structure of the new environment or `NULL`.
 */
REDUCT_API reduct_t* reduct_new(void);

/**
 * @brief Free the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure to free.
 */
REDUCT_API void reduct_free(reduct_t* reduct);

/**
 * @brief Set the user data pointer for the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param userdata The user data pointer.
 */
REDUCT_API void reduct_userdata_set(reduct_t* reduct, void* userdata);

/**
 * @brief Get the user data pointer from the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The user data pointer.
 */
REDUCT_API void* reduct_userdata_get(reduct_t* reduct);

/**
 * @brief Set the command line arguments for the Reduct structure.
 *
 * Will be utilized by the `(args!)` native.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param argc The number of arguments.
 * @param argv The argument strings.
 */
REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv);

/**
 * @brief Hash a string.
 *
 * @param str The string to hash.
 * @param len The length of the string.
 * @return The hash of the string.
 */
static inline REDUCT_ALWAYS_INLINE uint32_t reduct_hash(const char* str, size_t len)
{
    uint32_t hash = REDUCT_FNV_OFFSET;
    for (size_t i = 0; i < len; i++)
    {
        hash ^= (unsigned char)str[i];
        hash *= REDUCT_FNV_PRIME;
    }
    return hash;
}

/** @} */

#endif
