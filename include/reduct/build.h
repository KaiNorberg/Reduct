#ifndef REDUCT_BUILD_H
#define REDUCT_BUILD_H 1

#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/list.h>
#include <reduct/native.h>
#include <reduct/rvsdg.h>

struct reduct;
struct reduct_item;

/**
 * @file build.h
 * @brief Intermediate Representation builder.
 * @defgroup build Build
 *
 * The builder is responsible for converting parsed S-expressions (ASTs) into the intermediate representation (IR) used
 * by the optimizer and compiler.
 *
 * @{
 */

/**
 * @brief Build an IR graph from an AST (parsed S-expression).
 *
 * @param reduct Pointer to the Reduct structure.
 * @param ast Handle to the root of the AST.
 * @return Handle to the root IR node.
 */
REDUCT_API reduct_handle_t reduct_build(struct reduct* reduct, reduct_handle_t ast);

/**
 * @brief Get the native function associated with an opcode.
 *
 * @param op The opcode.
 * @return The native function, or NULL if not found.
 */
REDUCT_API reduct_native_fn reduct_builder_get_native_fn(reduct_opcode_t op);

/** @} */

#endif
