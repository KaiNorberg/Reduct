#ifndef REDUCT_NATIVE_H
#define REDUCT_NATIVE_H 1

#include "reduct/defs.h"

struct reduct;
struct reduct_compiler;
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
 * @brief Native function pointer type.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param argc The number of arguments.
 * @param argv The array of arguments.
 * @return The result of the function.
 */
typedef reduct_handle_t (*reduct_native_fn)(struct reduct* reduct, size_t argc, reduct_handle_t* argv);

/**
 * @brief Intrinsic handler function type.
 *
 * @param compiler Pointer to the compiler context.
 * @param expr The expression being compiled.
 * @param out The output expression.
 */
typedef void (
    *reduct_native_intrinsic_fn)(struct reduct_compiler* compiler, struct reduct_list* expr, struct reduct_expr* out);

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
REDUCT_API void reduct_native_register(struct reduct* reduct, reduct_native_t* array, size_t count);

/** @} */

#endif
