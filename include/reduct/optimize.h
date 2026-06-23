#ifndef REDUCT_OPTIMIZE_H
#define REDUCT_OPTIMIZE_H 1

#include <reduct/defs.h>
#include <reduct/inst.h>

#include <stdio.h>

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
    REDUCT_OPTIMIZE_NONE = 0,                          ///< No optimization flags.
    REDUCT_OPTIMIZE_CONSTANT_FOLDING = 1 << 1,         ///< Constant folding.
    REDUCT_OPTIMIZE_FUNCTION_INLINING = 1 << 2,        ///< Function inlining.
    REDUCT_OPTIMIZE_CSE = 1 << 3,                      ///< Common subexpression elimination.
    REDUCT_OPTIMIZE_ALGEBRAIC_SIMPLIFICATION = 1 << 4, ///< Algebraic simplification.
    REDUCT_OPTIMIZE_GAMMA_FOLDING = 1 << 5,            ///< Branch folding for Gamma nodes.
    REDUCT_OPTIMIZE_AUTO_PARALLELIZATION = 1 << 6,     ///< Automatic parallelization of independent call nodes.
    REDUCT_OPTIMIZE_ALL = 0xFFFFFFFF,                  ///< Enable all optimizations.

    REDUCT_OPTIMIZE_O1 = REDUCT_OPTIMIZE_ALGEBRAIC_SIMPLIFICATION, ///< Level 1 optimizations.
    REDUCT_OPTIMIZE_O2 = REDUCT_OPTIMIZE_CONSTANT_FOLDING | REDUCT_OPTIMIZE_CSE |
        REDUCT_OPTIMIZE_ALGEBRAIC_SIMPLIFICATION | REDUCT_OPTIMIZE_GAMMA_FOLDING, ///< Level 2 optimizations.
    REDUCT_OPTIMIZE_O3 = REDUCT_OPTIMIZE_ALL,                                     ///< Level 3 optimizations (maximum).
} reduct_optimize_flags_t;

/**
 * @brief Global optimization-related state structure.
 * @struct reduct_optimize_global_t
 */
typedef struct
{
    reduct_optimize_flags_t lastFlags;
} reduct_optimize_global_t;

/**
 * @brief Initialize a global optimize state.
 *
 * @param global Pointer to the global optimize state to initialize.
 */
REDUCT_API void reduct_optimize_global_init(reduct_optimize_global_t* global);

/**
 * @brief Deinitialize a global optimize state.
 *
 * @param global Pointer to the global optimize state to deinitialize.
 */
REDUCT_API void reduct_optimize_global_deinit(reduct_optimize_global_t* global);

/**
 * @brief Optimize a built IR graph.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle Handle to the root node of the IR graph to optimize.
 * @param flags Optimization flags to control which optimizations are applied.
 */
REDUCT_API void reduct_optimize(struct reduct* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags);

/** @} */

#endif
