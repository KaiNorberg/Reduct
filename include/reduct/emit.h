#ifndef REDUCT_EMIT_H
#define REDUCT_EMIT_H 1

#include <reduct/bitmap.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/item.h>
#include <reduct/list.h>

/**
 * @file emit.h
 * @brief Bytecode Emission
 * @defgroup emit Emission
 *
 * The emitter converts IR into register-based bytecode that can be executed by the Reduct virtual machine / evaluator.
 *
 * @{
 */

/**
 * @brief Emitter expression type.
 * @enum reduct_emitter_expr_type_t
 */
typedef enum
{
    REDUCT_EMITTER_EXPR_TYPE_NONE,  ///< No expression.
    REDUCT_EMITTER_EXPR_TYPE_REG,   ///< Expression is in a register.
    REDUCT_EMITTER_EXPR_TYPE_CONST, ///< Expression is a constant.
} reduct_emitter_expr_type_t;

/**
 * @brief Emitter expression descriptor structure.
 * @struct reduct_emitter_expr_t
 */
typedef struct reduct_emitter_expr
{
    reduct_emitter_expr_type_t type;    ///< Expression type.
    struct reduct_rvsdg_origin* origin; ///< The origin this expression represents, if any.
    union {
        reduct_reg_t reg;        ///< Register index.
        reduct_const_t constant; ///< Constant index.
        uint16_t raw;            ///< Raw union value.
    };
} reduct_emitter_expr_t;

/**
 * @brief Create an emitter expression from an instruction.
 *
 * @param _inst The instruction.
 */
#define REDUCT_EMITTER_EXPR_INST(_inst) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_INST, .origin = NULL, .inst = (_inst)})

/**
 * @brief Create an emitter expression from a register.
 *
 * @param _reg The register index.
 */
#define REDUCT_EMITTER_EXPR_REG(_reg) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_REG, .origin = NULL, .reg = (_reg)})

/**
 * @brief Create an emitter expression from a constant.
 *
 * @param _const The constant index.
 */
#define REDUCT_EMITTER_EXPR_CONST(_const) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_CONST, .origin = NULL, .constant = (_const)})

/**
 * @brief Create an empty/none emitter expression.
 */
#define REDUCT_EMITTER_EXPR_NONE ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_NONE, .origin = NULL})

/**
 * @brief Emitter cache entry for node de-duplication.
 */
typedef struct reduct_emitter_cache_entry
{
    struct reduct_rvsdg_origin* origin; ///< The origin this cache entry represents.
    reduct_emitter_expr_t expr;
    uint16_t remainingUses; ///< Number of users still needing this result.
} reduct_emitter_cache_entry_t;

/**
 * @brief Emitter structure.
 * @struct reduct_emitter_t
 */
typedef struct reduct_emitter
{
    reduct_bitmap_t registers[REDUCT_BITMAP_SIZE(REDUCT_REGISTER_MAX)]; ///< Bitmap of allocated registers.
    struct reduct* reduct;                                              ///< Pointer to the Reduct structure.
    reduct_function_t* function;                                        ///< The function being compiled.
    struct reduct_rvsdg_node* phiNode;                                  ///< The PHI node defining recursion.
    reduct_emitter_cache_entry_t* cache;                                ///< Node emission cache.
    size_t cacheCount;                                                  ///< Number of cached nodes.
    size_t cacheCapacity;                                               ///< Capacity of the cache.
    struct reduct_item* lastItem;                                       ///< The last item processed by the emitter.
} reduct_emitter_t;

/**
 * @brief Compile Reduct IR into a callable bytecode function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param graph Handle to the root of the IR graph.
 * @return Handle to the compiled function.
 */
REDUCT_API reduct_handle_t reduct_emit(struct reduct* reduct, reduct_handle_t graph);

/** @} */

#endif
