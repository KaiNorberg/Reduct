#ifndef REDUCT_NATIVE_H
#define REDUCT_NATIVE_H 1

#include <reduct/defs.h>
#include <reduct/rvsdg.h>
#include <reduct/sync.h>

struct reduct;
struct reduct_builder;
struct reduct_item;
struct reduct_expr;

/**
 * @file native.h
 * @brief Native function and intrinsic registration.
 * @defgroup native Native Functions and Intrinsics
 *
 * A "native" is a C function that can be called at runtime, each native may have an associated "intrinsic", which is a
 * function that is called during compilation.
 *
 * Both are stored in a unified hash map keyed by name.
 *
 * @see intrinsic
 *
 * @{
 */

/**
 * @brief Native function definition structure.
 */
typedef struct
{
    const char* name;
    reduct_native_fn nativeFn;
    reduct_native_intrinsic_fn intrinsicFn;
} reduct_native_t;

#define REDUCT_NATIVE_MAP_INITIAL 256 ///< The initial size of the native map.
#define REDUCT_NATIVE_MAP_GROWTH 2    ///< The growth factor of the native map.

/**
 * @brief Global native-related environment structure.
 * @struct reduct_native_env_t
 */
typedef struct
{
    struct reduct_native_entry* map;
    size_t size;
    size_t capacity;
    size_t mask;
    reduct_rwmutex_t mutex;
} reduct_native_env_t;

/**
 * @brief Initialize a native environment.
 *
 * @param state Pointer to the native environment to initialize.
 */
REDUCT_API void reduct_native_env_init(reduct_native_env_t* env);

/**
 * @brief Deinitialize a native environment.
 *
 * @param state Pointer to the native environment to deinitialize.
 */
REDUCT_API void reduct_native_env_deinit(reduct_native_env_t* env);

/**
 * @brief Native map entry.
 */
typedef struct reduct_native_entry
{
    uint32_t hash;
    uint32_t length;
    reduct_native_fn nativeFn;
    reduct_native_intrinsic_fn intrinsicFn;
    char* name;
} reduct_native_entry_t;

/**
 * @brief Find a native entry in the map.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param hash The hash of the name.
 * @param str The name string.
 * @param len The length of the name.
 * @return A pointer to the native entry, or `NULL` if not found.
 */
REDUCT_API reduct_native_entry_t* reduct_native_map_find(struct reduct* reduct, uint32_t hash, const char* str,
    size_t len);

/**
 * @brief Register native functions.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param array An array of native function definitions.
 * @param count The number of functions in the array.
 */
REDUCT_API void reduct_native_register(struct reduct* reduct, const reduct_native_t* array, size_t count);

/** @} */

#endif
