#ifndef REDUCT_MODULE_H
#define REDUCT_MODULE_H 1

#include <reduct/defs.h>
#include <reduct/sync.h>

#include <stdbool.h>
#include <stddef.h>

/**
 * @file module.h
 * @brief Module system
 * @defgroup module
 *
 * The modules keep track of which file an item originates from for error reporting, or tracks resources such as open
 * shared libraries.
 *
 * @{
 */

/**
 * @brief Module initialization function type.
 */
typedef reduct_handle_t (*reduct_module_init_fn)(struct reduct* reduct);

#define REDUCT_MODULE_ENTRY "reduct_module_init" ///< The name of the entry symbol for a Reduct module.

#define REDUCT_MODULE_PATHS_INITIAL 4 ///< Initial size of the module path array.
#define REDUCT_MODULE_PATHS_GROWTH 2  ///< Growth factor of the module path array.

/**
 * @brief Identifies a `reduct_module_t` within a Reduct structure.
 *
 * Avoids the need to store a `reduct_module_t*` within a `reduct_item_t` saving space.
 */
typedef uint16_t reduct_module_id_t;

/**
 * @brief Invalid handle value.
 */
#define REDUCT_MODULE_ID_NONE ((reduct_module_id_t) - 1)

/**
 * @brief Module flags.
 * @struct reduct_module_flags_t
 */
typedef enum reduct_module_flags
{
    REDUCT_MODULE_FLAG_NONE = 0,
    REDUCT_MODULE_FLAG_BUFFER_OWNED = 1 << 0, ///< The buffer is owned by the module structure and should be freed.
    REDUCT_MODULE_FLAG_IS_LIBRARY = 1 << 1    ///< The module is a loaded shared library.
} reduct_module_flags_t;

/**
 * @brief Module structure.
 * @struct reduct_module_t
 *
 * Represents either a source module or a loaded shared library.
 * Source modules have a buffer and optional AST, while library modules
 * hold a reference to the loaded shared library handle.
 */
typedef struct reduct_module
{
    struct reduct_module* prev;
    reduct_handle_t ast;
    const char* buffer;
    const char* end;
    reduct_module_id_t id;
    reduct_module_flags_t flags;
    char* path;
    reduct_lib_t lib; ///< Loaded shared library handle, must have `REDUCT_MODULE_FLAG_IS_LIBRARY` set.
} reduct_module_t;

/**
 * @brief Global module state structure.
 * @struct reduct_module_global_t
 */
typedef struct
{
    reduct_rwmutex_t moduleMutex;
    reduct_module_t* moduleHead;
    reduct_module_id_t moduleNextId;
    reduct_rwmutex_t pathsMutex;
    char** paths;
    size_t pathCount;
    size_t pathCapacity;
} reduct_module_global_t;

/**
 * @brief Initialize the global module state.
 *
 * @param global Pointer to the global module state structure.
 */
REDUCT_API void reduct_module_global_init(reduct_module_global_t* global);

/**
 * @brief Deinitialize the global module state.
 *
 * @param global Pointer to the global module state structure.
 */
REDUCT_API void reduct_module_global_deinit(reduct_module_global_t* global);

/**
 * @brief Create a new module to track a buffer.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param buffer The input buffer containing source code.
 * @param length The length of the input buffer.
 * @param path The path to the input file (for error reporting and relative imports).
 * @param flags Input flags (e.g., REDUCT_MODULE_FLAG_BUFFER_OWNED).
 * @return A pointer to the newly created module structure.
 */
REDUCT_API reduct_module_t* reduct_module_new(struct reduct* reduct, const char* buffer, size_t length,
    const char* path, reduct_module_flags_t flags);

/**
 * @brief Lookup a module structure by its ID.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the module structure.
 * @return A pointer to the module structure, or NULL if not found.
 */
REDUCT_API reduct_module_t* reduct_module_lookup(struct reduct* reduct, reduct_module_id_t id);

/**
 * @brief Add a path to search when importing modules.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param path The directory path, will be copied.
 */
REDUCT_API void reduct_module_add_path(struct reduct* reduct, const char* path);

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
REDUCT_API void reduct_module_resolve_path(struct reduct* reduct, const char* path, size_t pathLen, char* outPath,
    size_t maxLen, bool checkExistence);

/**
 * @brief Import a module from a given path.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param path The path to the module file.
 * @param compiler Handle to an atom storing the name of the compiler to use.
 * @param compilerArgs Handle to an atom storing the compiler arguments.
 * @return A handle to the root of the parsed module AST.
 */
REDUCT_API reduct_handle_t reduct_module_import(struct reduct* reduct, reduct_handle_t path, reduct_handle_t compiler,
    reduct_handle_t compilerArgs);

/** @} */

#endif
