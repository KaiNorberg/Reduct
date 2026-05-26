#ifndef REDUCT_BUILD_H
#define REDUCT_BUILD_H 1

#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/list.h>
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
 * @brief Builder local structure.
 * @struct reduct_builder_local_t
 */
typedef struct reduct_builder_local
{
    struct reduct_atom* key;
    struct reduct_rvsdg_origin* value;
} reduct_builder_local_t;

/**
 * @brief Builder scope structure.
 * @struct reduct_builder_scope_t
 */
typedef struct reduct_builder_scope
{
    reduct_builder_local_t locals[REDUCT_REGISTER_MAX]; ///< The local variables in the current scope.
    uint64_t localAmount;                               ///< The amount of allocated locals.
    struct reduct_rvsdg_region* region;                 ///< The region to add new nodes to, `NULL`.
    struct reduct_builder_scope* parent;                ///< The parent scope.
    struct reduct_rvsdg_node* lambdaNode;               ///< The lambda node this scope belongs to.
} reduct_builder_scope_t;

/**
 * @brief IR builder context.
 * @struct reduct_builder_t
 */
typedef struct reduct_builder
{
    struct reduct* reduct;         ///< Pointer to the Reduct structure.
    struct reduct_item* lastItem;  ///< The last item processed by the builder, used for error reporting.
    reduct_builder_scope_t* scope; ///< The current scope.
} reduct_builder_t;

/**
 * @brief Build an IR graph from an AST (parsed S-expression).
 *
 * @param reduct Pointer to the Reduct structure.
 * @param ast Handle to the root of the AST.
 * @return Handle to the root IR node.
 */
REDUCT_API reduct_handle_t reduct_build(struct reduct* reduct, reduct_handle_t ast);

/**
 * @brief Build an IR node from a handle, dispatching based on the handle type.
 *
 * @param builder Pointer to the builder context.
 * @param handle The handle to build.
 * @return Pointer to the first ouput of the built IR node.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_build_handle(reduct_builder_t* builder, reduct_handle_t handle);

/**
 * @brief Build an IR node from a list as if it was a `list` block.
 *
 * @param builder Pointer to the builder context.
 * @param list Handle to the list.
 * @param startIdx The index in the list to start building from.
 * @return Pointer to the first ouput of the built IR node.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_build_generic_list(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx);

/**
 * @brief Build an IR node from a list as if it was a `do` block.
 *
 * @param builder Pointer to the builder context.
 * @param list Handle to the list.
 * @param startIdx The index in the list to start building from.
 * @return Pointer to the first ouput of the built IR node.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_build_generic_block(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx);

/** @} */

#endif
