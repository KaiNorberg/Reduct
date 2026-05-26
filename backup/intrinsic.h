#ifndef REDUCT_INTRINSIC_H
#define REDUCT_INTRINSIC_H 1

#include <reduct/defs.h>
#include <reduct/native.h>

struct reduct;
struct reduct_compiler;
struct reduct_item;
struct reduct_expr;
struct reduct_list;

/**
 * @file intrinsic.h
 * @brief Intrinsic management.
 * @defgroup intrinsic Intrinsics
 *
 * A intrinsic is an atom recognized by the bytecode compiler as a special form or built-in operator, resulting in it
 * emitting specific bytecode instructions for each intrinsic.
 *
 * This is in contrast to a "native" which will be invoked as a standard function call at runtime.
 *
 * @{
 */

/**
 * @brief Registers all Reduct intrinsics with the given Reduct instance.
 *
 * @param reduct The Reduct instance to register intrinsics with.
 */
REDUCT_API void reduct_intrinsic_register_all(struct reduct* reduct);

/**
 * @brief Compiles a block of expressions (like a `do` block).
 *
 * @param compiler The compiler context.
 * @param list The list containing the expressions.
 * @param startIdx The index to start compiling from.
 * @param out The output expression.
 */
REDUCT_API void reduct_intrinsic_block_generic(struct reduct_compiler* compiler, struct reduct_list* list,
    uint32_t startIdx, struct reduct_expr* out);

/**
 * @brief Compiles a list of expressions into a list.
 *
 * @param compiler The compiler context.
 * @param list The list containing the expressions to be evaluated and added to the new list.
 * @param startIdx The index to start compiling from.
 * @param out The output expression.
 */
REDUCT_API void reduct_intrinsic_list_generic(struct reduct_compiler* compiler, struct reduct_list* list,
    uint32_t startIdx, struct reduct_expr* out);

#endif
