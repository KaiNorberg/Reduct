#ifndef REDUCT_OPTIMIZE_H
#define REDUCT_OPTIMIZE_H 1

#include "reduct/defs.h"

struct reduct;

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
    REDUCT_OPTIMIZE_NONE = 0,                  ///< No optimization flags.
    REDUCT_OPTIMIZE_PEEPHOLE = 1 << 0,         ///< Enable peephole optimizations.
    REDUCT_OPTIMIZE_CONSTANT_FOLDING = 1 << 1, ///< Enable constant folding optimizations.
    REDUCT_OPTIMIZE_DEAD_STORE = 1 << 2,       ///< Enable dead store optimizations.
    REDUCT_OPTIMIZE_MOV_PROPAGATE = 1 << 3,    ///< Enable move propagation optimizations.
    REDUCT_OPTIMIZE_DEAD_CODE = 1 << 4,        ///< Enable dead/unreachable code elimination.
    REDUCT_OPTIMIZE_ALGEBRAIC = 1 << 5,        ///< Enable algebraic optimizations.
    REDUCT_OPTIMIZE_ALL = 0xFFFFFFFF,          ///< Enable all optimizations.

    REDUCT_OPTIMIZE_O1 = REDUCT_OPTIMIZE_PEEPHOLE | REDUCT_OPTIMIZE_CONSTANT_FOLDING |
        REDUCT_OPTIMIZE_ALGEBRAIC, ///< Level 1 optimizations.
    REDUCT_OPTIMIZE_O2 = REDUCT_OPTIMIZE_O1 | REDUCT_OPTIMIZE_DEAD_STORE | REDUCT_OPTIMIZE_MOV_PROPAGATE |
        REDUCT_OPTIMIZE_DEAD_CODE,            ///< Level 2 optimizations.
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