#ifndef REDUCT_OPTIMIZE_H
#define REDUCT_OPTIMIZE_H 1

#include <reduct/defs.h>
#include <reduct/inst.h>

#include <stdio.h>

struct reduct;
struct reduct_optimize_origin;
struct reduct_optimize_user;
struct reduct_optimize_edge;

/**
 * @file optimize.h
 * @brief Bytecode optimization.
 * @defgroup optimize Optimization
 *
 * @{
 */

/**
 * @brief Optimization flags.
 * @enum reduct_optimize_flags_t
 */
typedef enum reduct_optimize_flags
{
    REDUCT_OPTIMIZE_NONE = 0,         ///< No optimization flags.
    REDUCT_OPTIMIZE_ALL = 0xFFFFFFFF, ///< Enable all optimizations.

    REDUCT_OPTIMIZE_O1 = 0,                   ///< Level 1 optimizations.
    REDUCT_OPTIMIZE_O2 = 0,                   ///< Level 2 optimizations.
    REDUCT_OPTIMIZE_O3 = REDUCT_OPTIMIZE_ALL, ///< Level 3 optimizations (maximum).
} reduct_optimize_flags_t;

/**
 * @brief Optimize a compiled function and its child functions.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle Handle to the function to optimize.
 * @param flags Optimization flags to control which optimizations are applied.
 */
REDUCT_API void reduct_optimize(struct reduct* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags);

/** @} */

#endif