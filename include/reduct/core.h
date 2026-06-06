#include "reduct/emit.h"
#ifndef REDUCT_CORE_H
#define REDUCT_CORE_H 1

#include <reduct/atom.h>
#include <reduct/defs.h>
#include <reduct/error.h>
#include <reduct/eval.h>
#include <reduct/item.h>
#include <reduct/list.h>
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

#define REDUCT_IMPORT_PATHS_INITIAL 4 ///< Initial size of the import path array.
#define REDUCT_IMPORT_PATHS_GROWTH 2  ///< Growth factor of the import path array.

#define REDUCT_SCHEMA_INITIAL 4 ///< Initial size of the schema array.
#define REDUCT_SCHEMA_GROWTH 2  ///< Growth factor of the schema array.

#define REDUCT_CONSTANTS_MAX 8 ///< Maximum amount of predefined constants.

/**
 * @brief Input flags.
 */
typedef enum
{
    REDUCT_INPUT_FLAG_NONE = 0,
    REDUCT_INPUT_FLAG_OWNED = 1 ///< The input buffer is owned by the input structure and should be freed.
} reduct_input_flags_t;

/**
 * @brief Input structure.
 * @struct reduct_input_t
 */
typedef struct reduct_input
{
    struct reduct_input* prev;
    reduct_handle_t ast;
    const char* buffer;
    const char* end;
    reduct_input_id_t id;
    reduct_input_flags_t flags;
    char path[REDUCT_PATH_MAX];
} reduct_input_t;

/**
 * @brief Global input-related state structure.
 * @struct reduct_input_global_t
 */
typedef struct
{
    reduct_rwmutex_t mutex;
    reduct_input_t* head;
    reduct_input_id_t nextId;
} reduct_input_global_t;

/**
 * @brief Global import-related state structure.
 * @struct reduct_import_global_t
 */
typedef struct
{
    reduct_rwmutex_t mutex;
    char** paths;
    size_t count;
    size_t capacity;
} reduct_import_global_t;

/**
 * @brief Global library-related state structure.
 * @struct reduct_lib_global_t
 */
typedef struct
{
    reduct_rwmutex_t mutex;
    reduct_lib_t* array;
    size_t count;
    size_t capacity;
} reduct_lib_global_t;

#define REDUCT_SCRATCH_INITIAL 128 ///< Initial scratch buffer size.
#define REDUCT_SCRATCH_MAX 16      ///< The maximum number of scratch buffers.

#define REDUCT_LIBS_INITIAL 4 ///< Initial size of the library array.
#define REDUCT_LIBS_GROWTH 2  ///< Growth factor of the library array.

/**
 * @brief Global state structure.
 * @struct reduct_global_t
 */
typedef struct reduct_global
{
    int argc;
    char** argv;
    reduct_input_global_t input;
    reduct_import_global_t import;
    reduct_lib_global_t lib;
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
    reduct_handle_t nil;
    reduct_atom_local_t atom;
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
 * @brief Add a loaded library handle to the global state.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param lib The library handle.
 */
REDUCT_API void reduct_global_lib_add(reduct_t* reduct, reduct_lib_t lib);

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
 * @brief Add a path to search when importing modules.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param path The directory path, will be copied.
 */
REDUCT_API void reduct_add_import_path(reduct_t* reduct, const char* path);

/**
 * @brief Create a new input structure and push it onto the input stack.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param buffer The input buffer.
 * @param length The length of the input buffer.
 * @param path The path to the input file.
 * @param flags Input flags.
 * @return A pointer to the newly created input structure.
 */
REDUCT_API reduct_input_t* reduct_input_new(reduct_t* reduct, const char* buffer, size_t length, const char* path,
    reduct_input_flags_t flags);

/**
 * @brief Lookup an input structure by its ID.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the input structure.
 * @return A pointer to the input structure, or `NULL` if not found.
 */
REDUCT_API reduct_input_t* reduct_input_lookup(reduct_t* reduct, reduct_input_id_t id);

/**
 * @brief Resolve a path relative to the current execution frame or import paths.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param path The path string to resolve.
 * @param pathLen The length of the path string.
 * @param outPath Pointer to the buffer where the resolved path will be stored.
 * @param maxLen The maximum length of the output buffer.
 * @param checkExistence Whether to check if the file exists when resolving relative paths.
 */
REDUCT_API void reduct_resolve_path(reduct_t* reduct, const char* path, size_t pathLen, char* outPath, size_t maxLen,
    bool checkExistence);

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
