#ifndef REDUCT_OPTIMIZE_H
#define REDUCT_OPTIMIZE_H 1

#include "reduct/defs.h"
#include "reduct/inst.h"

struct reduct;
struct reduct_optimize_origin;
struct reduct_optimize_user;
struct reduct_optimize_edge;

/**
 * @file optimize.h
 * @brief Bytecode optimization.
 * @defgroup optimize Optimization
 *
 * The optimizer uses a IR (Intermediate representation) heavily inspired by the RVSDG (Regionalized Value State
 * Dependence Graph).
 *
 * ## Nodes, Regions and Edges
 *
 * A RVSDG representation is made up of nodes, regions and edges. Nodes represent operations (addition, subtraction,
 * branches, functions, etc.), regions represent computations (a sequence of nodes) and edges represent data
 * dependencies between nodes.
 *
 * Each node can have any number of inputs or outputs and each region can have any number of arguments or results.
 *
 * The outputs of nodes and the arguments of regions are the origins of edges, while the input or results of a region
 * are the users of edges.
 *
 * ## Structure
 *
 * Included is a simple description of how data flows through the RVSDG:
 *
 * - A node takes in some number of inputs which get passed as arguments to the regions within the node (details depend
 * on node type, see below)
 * - A region contains some number of nodes which connect to its results and may connect to its arguments.
 * - The results of the region get passed as the outputs of the node (details depend on node type, see below).
 * - The outputs of the node can then connect to other nodes or be returned as results of the parent region.
 *
 * ## Node Types
 *
 * A node can either be simple or structural, a simple node represents a primitive operation such as addition or
 * subtraction. Structural nodes contain regions and represent more complex logic such as function calls, loops or
 * conditionals.
 *
 * There are multiple types of structural node which defines how it passes data to and from the regions it might
 * contain. Included below is a list of all such types.
 *
 * @note Since Reduct is immutable and does not have traditional global variables, some node types, for example
 * delta-nodes, will not be used.
 *
 * ### Gamma-Nodes
 *
 * A gamma node represents a branch or decision point. The first input to a gamma node is a predicate, which should
 * evaluate to a positive integer representing the index of the region to execute. The remaining inputs are passed as
 * arguments to the corresponding region with that regions outputs mapped to the gamma-nodes outputs.
 *
 * ### Lambda-Nodes
 *
 * A lambda node represents a function and contains a single region representing a function's body. The inputs are any
 * variables that the function depends on, including arguments and any captured variables. The single output is the
 * function itself, not the result of the function.
 *
 * @note The paper describes an "apply" node to represent a function invocation. For simplicity, this is represented as
 * simple `REDUCT_OPCODE_CALL` or `REDUCT_OPCODE_CALL_CONST` node.
 *
 * @see https://arxiv.org/abs/1912.05036 "RVSDG: An Intermediate Representation for Optimizing Compilers" (Nico
 * Reissmann et al., 2020)
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
 * @brief Owner of a data dependency origin or user.
 * @enum reduct_optimize_owner_kind_t
 */
typedef enum
{
    REDUCT_OPTIMIZE_OWNER_NODE,
    REDUCT_OPTIMIZE_OWNER_REGION,
} reduct_optimize_owner_kind_t;

/**
 * @brief Origin of a data dependency edge.
 * @struct reduct_optimize_origin_t
 */
typedef struct reduct_optimize_origin
{
    reduct_optimize_owner_kind_t ownerKind; ///< The kind of owner (node or region).
    union {
        struct reduct_optimize_node* node;     ///< The node this origin belongs to.
        struct reduct_optimize_region* region; ///< The region this origin belongs to.
    };
    uint16_t index;                    ///< The index for the associated output/argument.
    uint16_t useCount;                 ///< Length of the `uses` list.
    struct reduct_optimize_edge* uses; ///< List of edges originating from this output/argument.
} reduct_optimize_origin_t;

/**
 * @brief User of a data dependency edge.
 * @struct reduct_optimize_user_t
 */
typedef struct reduct_optimize_user
{
    reduct_optimize_owner_kind_t ownerKind; ///< The kind of owner (node or region).
    union {
        struct reduct_optimize_node* node;     ///< The node this user belongs to.
        struct reduct_optimize_region* region; ///< The region this user belongs to.
    };
    uint16_t index;                   ///< The index for the associated input/result.
    struct reduct_optimize_edge* use; ///< The single edge connecting to this input's/result's origin.
} reduct_optimize_user_t;

/**
 * @brief Edge structure representing a data dependency.
 * @struct reduct_optimize_edge_t
 */
typedef struct reduct_optimize_edge
{
    struct reduct_optimize_origin* origin; ///< The node where the edge originates.
    struct reduct_optimize_user* user;     ///< The node where the edge ends.
    struct reduct_optimize_edge* next;     ///< The next edge in the list.
    struct reduct_optimize_edge* prev;     ///< The previous edge in the list.
} reduct_optimize_edge_t;

/**
 * @brief Node type.
 * @enum reduct_optimize_node_type_t
 */
typedef enum
{
    REDUCT_OPTIMIZE_NODE_TYPE_INVALID = 0, ///< Invalid node type.
    REDUCT_OPTIMIZE_NODE_TYPE_SIMPLE_OPCODE,      ///< Represents a primitive operation (specifically an opcode)
    REDUCT_OPTIMIZE_NODE_TYPE_SIMPLE_CONST, ///< Represents a constant in the constant table.
    REDUCT_OPTIMIZE_NODE_TYPE_GAMMA,       ///< Represents a branch or decision point.
    REDUCT_OPTIMIZE_NODE_TYPE_LAMBDA       ///< Represents a function.
} reduct_optimize_node_type_t;

/**
 * @brief A node in the RVSDG.
 * @enum reduct_optimize_node_t
 */
typedef struct reduct_optimize_node
{
    reduct_optimize_node_type_t type;       ///< The type of the node.
    reduct_opcode_t opcode;                 ///< The opcode associated with the node.
    reduct_const_t constant;                ///< The constant associated with the node.
    uint16_t inputCount;                    ///< Number of input edges.
    uint16_t outputCount;                   ///< Number of output edges.
    uint16_t regionCount;                   ///< Number of regions in the node (only for structural nodes).
    reduct_optimize_user_t* inputs;         ///< Array of input ports.
    reduct_optimize_origin_t* outputs;      ///< Array of output ports.
    struct reduct_optimize_region* regions; ///< Array of regions in the node (only for structural nodes).
} reduct_optimize_node_t;

/**
 * @brief Represents a computation.
 * @enum reduct_optimize_region_t
 */
typedef struct reduct_optimize_region
{
    uint16_t argumentCount;              ///< Number of arguments to the region.
    uint16_t resultCount;                ///< Number of result values from the region.
    reduct_optimize_origin_t* arguments; ///< Array of argument ports.
    reduct_optimize_user_t* results;     ///< Array of result ports.
} reduct_optimize_region_t;

/**
 * @brief Represents the entire RVSDG.
 * @enum reduct_optimize_graph_t
 */
typedef struct reduct_optimize_graph
{
    reduct_optimize_node_t* root;
} reduct_optimize_graph_t;

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