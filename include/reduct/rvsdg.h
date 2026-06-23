#ifndef REDUCT_RVSDG_H
#define REDUCT_RVSDG_H 1

#include <reduct/defs.h>
#include <reduct/inst.h>

#include <stdbool.h>
#include <stdio.h>

struct reduct;
struct reduct_rvsdg_origin;
struct reduct_rvsdg_user;
struct reduct_rvsdg_edge;

/**
 * @file rvsdg.h
 * @brief Intermediate Representation
 * @defgroup rvsdg RVSDG
 *
 * Reduct uses a IR (Intermediate representation) heavily inspired by the RVSDG (Regionalized Value State
 * Dependence Graph).
 *
 * ## Nodes, Regions and Edges
 *
 * A RVSDG representation is made up of nodes, regions and edges. Nodes represent operations (addition, subtraction,
 * branches, functions, etc.), regions represent computations (a sequence of nodes) and edges represent data
 * dependencies between nodes.
 *
 * Each node can have any number of inputs but will always have exactly one output. Each region can have any number of
 * arguments but will also always have exactly one result.
 *
 * The output of nodes and the arguments of regions are the origins of edges, while the input or results of a region
 * are the users of edges.
 *
 * @note Nodes having one output and regions having one result is a deviation from the paper, however, since Reduct is
 * immutable, we can know that any expression will always produce exactly one output/result. So we can simply things.
 *
 * ## Structure
 *
 * Included is a simple description of how data flows through the RVSDG:
 *
 * - A node takes in some number of inputs which get passed as arguments to the regions within the node (details depend
 * on node type, see below)
 * - A region contains some number of nodes which connect to its result and may connect to its arguments.
 * - The result of the region get passed as the outputs of the node (details depend on node type, see below).
 * - The output of the node can then connect to other nodes or be returned as results of the parent region.
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
 * delta nodes, will not be used. Omega nodes are also replaced with lambda nodes.
 *
 * ### Gamma Nodes
 *
 * A gamma node represents a branch or decision point. The first input to a gamma node is a predicate, which should
 * evaluate to a positive integer representing the index of the region to execute. The remaining inputs are passed as
 * arguments to the corresponding region with that regions outputs mapped to the gamma nodes outputs.
 *
 * ### Lambda Nodes
 *
 * A lambda node represents a function and contains a single region representing a function's body. The inputs are not
 * the arguments to the lambda but instead captured variables. The single output is the function itself, not the result
 * of the function.
 *
 * @note The paper describes an "apply" node to represent a function invocation. For simplicity, this is represented as
 * simple `REDUCT_OPCODE_CALL` or `REDUCT_OPCODE_CALL_CONST` node.
 *
 * ### Phi Nodes
 *
 * A phi node allows a function to recursively call itself. It contains a single region containing a lambda
 * node, the output of the lambda node should be connected to the result of this region with the arguments
 * of this region connected to the inputs of the lambda node. The phi node itself only takes inputs for captured
 * variables and a outputs the lambda.
 *
 * @note The paper describes a phi node as being able to contain multiple lambda nodes for mutual recursion. This will
 * not be needed within Reduct.
 *
 * @see https://arxiv.org/abs/1912.05036 "RVSDG: An Intermediate Representation for Optimizing Compilers" (Nico
 * Reissmann et al., 2020)
 *
 * @{
 */

/**
 * @brief Owner of a data dependency origin or user.
 * @enum reduct_rvsdg_owner_kind_t
 */
typedef enum
{
    REDUCT_RVSDG_OWNER_NODE,
    REDUCT_RVSDG_OWNER_REGION,
} reduct_rvsdg_owner_kind_t;

/**
 * @brief Origin of a data dependency edge.
 * @struct reduct_rvsdg_origin_t
 */
typedef struct reduct_rvsdg_origin
{
    reduct_rvsdg_owner_kind_t ownerKind; ///< The kind of owner (node or region).
    union {
        struct reduct_rvsdg_node* node;     ///< The node this origin belongs to.
        struct reduct_rvsdg_region* region; ///< The region this origin belongs to.
    };
    uint16_t index;                   ///< The index for the associated output/argument.
    uint16_t useCount;                ///< Length of the `uses` list.
    struct reduct_rvsdg_edge* edges;  ///< List of edges originating from this output/argument.
    struct reduct_rvsdg_origin* next; ///< Next origin in the node/region list.
} reduct_rvsdg_origin_t;

/**
 * @brief User of a data dependency edge.
 * @struct reduct_rvsdg_user_t
 */
typedef struct reduct_rvsdg_user
{
    reduct_rvsdg_owner_kind_t ownerKind; ///< The kind of owner (node or region).
    union {
        struct reduct_rvsdg_node* node;     ///< The node this user belongs to.
        struct reduct_rvsdg_region* region; ///< The region this user belongs to.
    };
    uint16_t index;                 ///< The index for the associated input/result.
    struct reduct_rvsdg_edge* edge; ///< The single edge connecting to this input's/result's origin.
    struct reduct_rvsdg_user* next; ///< Next user in the node/region list.
} reduct_rvsdg_user_t;

/**
 * @brief Edge structure representing a data dependency.
 * @struct reduct_rvsdg_edge_t
 */
typedef struct reduct_rvsdg_edge
{
    struct reduct_rvsdg_origin* origin; ///< The node where the edge originates.
    struct reduct_rvsdg_user* user;     ///< The node where the edge ends.
    struct reduct_rvsdg_edge* next;     ///< The next edge in the list.
    struct reduct_rvsdg_edge* prev;     ///< The previous edge in the list.
} reduct_rvsdg_edge_t;

/**
 * @brief Node type.
 */
typedef uint8_t reduct_rvsdg_node_type_t;
#define REDUCT_RVSDG_NODE_TYPE_INVALID 0       ///< Invalid node type.
#define REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE 1 ///< Represents a primitive operation (opcode).
#define REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST 2  ///< Represents a constant.
#define REDUCT_RVSDG_NODE_TYPE_GAMMA 3         ///< Represents a branch or decision point.
#define REDUCT_RVSDG_NODE_TYPE_LAMBDA 4        ///< Represents a function.
#define REDUCT_RVSDG_NODE_TYPE_PHI 5           ///< Represents a phi node.

/**
 * @brief Node flags.
 */
typedef uint8_t reduct_rvsdg_node_flags_t;
#define REDUCT_RVSDG_NODE_FLAGS_NONE 0                   ///< No flags.
#define REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC (1 << 0) ///< Lambda node is variadic.

/**
 * @brief Information about a node type.
 * @struct reduct_rvsdg_node_info_t
 */
typedef struct reduct_rvsdg_node_info
{
    const char* name;        ///< The name of the node type.
    const char* color;       ///< The color of the node type for visualization.
    uint8_t dataInputOffset; ///< The index where data inputs begin (skipping control inputs).
} reduct_rvsdg_node_info_t;

/**
 * @brief Get information about a node type.
 *
 * @param type The node type.
 * @return Pointer to the node info structure.
 */
REDUCT_API const reduct_rvsdg_node_info_t* reduct_rvsdg_node_get_info(reduct_rvsdg_node_type_t type);

/**
 * @brief A node in the RVSDG.
 * @enum reduct_rvsdg_node_t
 */
typedef struct reduct_rvsdg_node
{
    reduct_rvsdg_node_type_t type;   ///< The type of the node.
    uint8_t inputCount;              ///< Number of input edges.
    uint8_t regionCount;             ///< Number of regions in the node.
    reduct_rvsdg_node_flags_t flags; ///< Node flags, interpretation depends on node type.
    uint8_t _reserved[4];
    struct reduct_rvsdg_user* firstInput;    ///< List of input ports.
    struct reduct_rvsdg_origin* output;      ///< The output port.
    struct reduct_rvsdg_region* firstRegion; ///< List of regions in the node.
    struct reduct_rvsdg_region* parent;      ///< The region this node belongs to.
    struct reduct_rvsdg_node* next;          ///< Next node in the region's list.
    union {
        reduct_opcode_t opcode;   ///< The opcode associated with the node.
        reduct_handle_t constant; ///< The constant value associated with the node.
    };
} reduct_rvsdg_node_t;

/**
 * @brief Represents a computation.
 * @enum reduct_rvsdg_region_t
 */
typedef struct reduct_rvsdg_region
{
    uint16_t argumentCount; ///< Number of arguments to the region.
    uint8_t _reserved[6];
    reduct_rvsdg_origin_t* firstArgument; ///< List of argument ports.
    reduct_rvsdg_user_t* result;          ///< The result port.
    struct reduct_rvsdg_node* firstNode;  ///< First node in the region.
    struct reduct_rvsdg_node* lastNode;   ///< Last node in the region.
    struct reduct_rvsdg_node* parent;     ///< The node that owns this region.
    struct reduct_rvsdg_region* next;     ///< Next region in the parent node's list.
} reduct_rvsdg_region_t;

/**
 * @brief Allocate a new IR edge.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The newly allocated edge.
 */
REDUCT_API reduct_rvsdg_edge_t* reduct_rvsdg_edge_new(struct reduct* reduct);

/**
 * @brief Connects an origin to a user via an edge.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param origin Pointer to the origin port.
 * @param user Pointer to the user port.
 */
REDUCT_API void reduct_rvsdg_edge_connect(struct reduct* reduct, reduct_rvsdg_origin_t* origin,
    reduct_rvsdg_user_t* user);

/**
 * @brief Disconnect an edge from its origin and user.
 *
 * @param edge Pointer to the edge to disconnect.
 */
REDUCT_API void reduct_rvsdg_edge_disconnect(reduct_rvsdg_edge_t* edge);

/**
 * @brief Allocate a new IR node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new(struct reduct* reduct);

/**
 * @brief Create a simple opcode node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param opcode The opcode to use.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_opcode(struct reduct* reduct,
    reduct_rvsdg_region_t* region, reduct_opcode_t opcode);

/**
 * @brief Create a simple constant node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param constant The constant to use.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_constant(struct reduct* reduct,
    reduct_rvsdg_region_t* region, reduct_handle_t constant);

/**
 * @brief Create a simple unary opcode node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param opcode The opcode to use.
 * @param input The origin of the input.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_unary(struct reduct* reduct, reduct_rvsdg_region_t* region,
    reduct_opcode_t opcode, struct reduct_rvsdg_origin* input);

/**
 * @brief Create a simple binary opcode node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param opcode The opcode to use.
 * @param left The origin of the left input.
 * @param right The origin of the right input.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_binary(struct reduct* reduct,
    reduct_rvsdg_region_t* region, reduct_opcode_t opcode, struct reduct_rvsdg_origin* left,
    struct reduct_rvsdg_origin* right);

/**
 * @brief Create a simple ternary opcode node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param opcode The opcode to use.
 * @param a The origin of the first input.
 * @param b The origin of the second input.
 * @param c The origin of the third input.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_ternary(struct reduct* reduct,
    reduct_rvsdg_region_t* region, reduct_opcode_t opcode, struct reduct_rvsdg_origin* a, struct reduct_rvsdg_origin* b,
    struct reduct_rvsdg_origin* c);

/**
 * @brief Create a lambda node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_lambda(struct reduct* reduct, reduct_rvsdg_region_t* region);

/**
 * @brief Create a phi node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_phi(struct reduct* reduct, reduct_rvsdg_region_t* region);

/**
 * @brief Create a gamma node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the node to, or NULL.
 * @param regionCount The number of regions (branches) to create.
 * @return The newly allocated node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_gamma(struct reduct* reduct, reduct_rvsdg_region_t* region,
    uint8_t regionCount);

/**
 * @brief Get an input port of a node by index.
 *
 * @param node The node to search.
 * @param index The index of the input port.
 * @return The user port, or NULL if not found.
 */
REDUCT_API struct reduct_rvsdg_user* reduct_rvsdg_node_get_input(reduct_rvsdg_node_t* node, uint16_t index);

/**
 * @brief Get the output of the node connected to an input node of a node by index.
 *
 * @param node The node to search.
 * @param index The index of the input port.
 * @return The origin port, or NULL if not found or not connected.
 */
REDUCT_API struct reduct_rvsdg_origin* reduct_rvsdg_node_get_input_origin(reduct_rvsdg_node_t* node, uint16_t index);

/**
 * @brief Get the node connected to an input node of a node by index.
 *
 * @param node The node to search.
 * @param index The index of the input port.
 * @return The input node, or NULL if not found or the port is not connected to a node.
 */
REDUCT_API struct reduct_rvsdg_node* reduct_rvsdg_node_get_input_node(reduct_rvsdg_node_t* node, uint16_t index);

/**
 * @brief Redirect all users of a origin to a new origin.
 *
 * @param origin The current origin.
 * @param newOrigin The new origin to redirect users to.
 */
REDUCT_API void reduct_rvsdg_origin_redirect_users(struct reduct_rvsdg_origin* origin,
    struct reduct_rvsdg_origin* newOrigin);

/**
 * @brief Get an argument port of a region by index.
 *
 * @param region The region to search.
 * @param index The index of the argument port.
 * @return The origin port, or NULL if not found.
 */
REDUCT_API struct reduct_rvsdg_origin* reduct_rvsdg_region_get_argument(reduct_rvsdg_region_t* region, uint16_t index);

/**
 * @brief Check if a region is an ancestor of another, or if they are the same.
 *
 * @param region The region to start from.
 * @param ancestor The potential ancestor region.
 * @return true if `ancestor` is an ancestor of (or the same as) `region`.
 */
REDUCT_API bool reduct_rvsdg_region_is_ancestor_or_same(reduct_rvsdg_region_t* region, reduct_rvsdg_region_t* ancestor);

/**
 * @brief Redirect an existing edge from its current origin to a new origin.
 *
 * @param edge The edge to redirect.
 * @param newOrigin The new origin to connect the edge to.
 */
REDUCT_API void reduct_rvsdg_edge_redirect(reduct_rvsdg_edge_t* edge, reduct_rvsdg_origin_t* newOrigin);

/**
 * @brief Wrap a lambda node in a phi node for recursive calls.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param lambda The lambda node to wrap.
 */
REDUCT_API void reduct_rvsdg_node_phi_wrap_lambda(struct reduct* reduct, reduct_rvsdg_node_t* lambda);

/**
 * @brief Check if a nodes grandparent is a phi node.
 *
 * @param node The node to check.
 * @return true if the node is nested inside a phi node's region.
 */
REDUCT_API bool reduct_rvsdg_node_is_inside_phi(reduct_rvsdg_node_t* node);

/**
 * @brief Create a CALL opcode node and connect a callable as its first input.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the call node to.
 * @param callable The origin representing the function to call.
 * @return The newly created call node.
 */
REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_call(struct reduct* reduct, reduct_rvsdg_region_t* region,
    reduct_rvsdg_origin_t* callable);

/**
 * @brief Map a node input index to a region argument index.
 *
 * @param node The parent node.
 * @param region The target region.
 * @param inputIndex The input index on the node.
 * @param outArgIndex Pointer to store the mapped argument index.
 * @return true if the input is mapped to an argument.
 */
REDUCT_API bool reduct_rvsdg_node_map_input_to_argument(reduct_rvsdg_node_t* node, struct reduct_rvsdg_region* region,
    uint16_t inputIndex, uint16_t* outArgIndex);

/**
 * @brief Map a region argument index to an input index of the parent node.
 *
 * @param node The parent node.
 * @param region The region containing the argument.
 * @param argIndex The argument index within the region.
 * @param outInputIndex Pointer to store the mapped input index.
 * @return true if the argument is mapped from an input.
 */
REDUCT_API bool reduct_rvsdg_node_map_argument_to_input(reduct_rvsdg_node_t* node, struct reduct_rvsdg_region* region,
    uint16_t argIndex, uint16_t* outInputIndex);

/**
 * @brief Check if an origin is a recursion target for a given phi node.
 *
 * @param node The phi node.
 * @param origin The origin to check.
 * @return true if the origin is a recursion target.
 */
REDUCT_API bool reduct_rvsdg_node_is_recur_origin(reduct_rvsdg_node_t* node, struct reduct_rvsdg_origin* origin);

/**
 * @brief Allocate a new IR region.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The newly allocated region.
 */
REDUCT_API reduct_rvsdg_region_t* reduct_rvsdg_region_new(struct reduct* reduct);

/**
 * @brief Allocate a new IR user port.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The newly allocated user port.
 */
REDUCT_API reduct_rvsdg_user_t* reduct_rvsdg_user_new(struct reduct* reduct);

/**
 * @brief Allocate a new IR origin port.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The newly allocated origin port.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_origin_new(struct reduct* reduct);

/**
 * @brief Add a new input port to a node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param node The node to add the input to.
 * @return The newly created user port.
 */
REDUCT_API reduct_rvsdg_user_t* reduct_rvsdg_node_add_input(struct reduct* reduct, reduct_rvsdg_node_t* node);

/**
 * @brief Add a new region to a node.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param node The node to add the region to.
 * @return The newly created region.
 */
REDUCT_API reduct_rvsdg_region_t* reduct_rvsdg_node_add_region(struct reduct* reduct, reduct_rvsdg_node_t* node);

/**
 * @brief Add a new argument port to a region.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The region to add the argument to.
 * @return The newly created origin port.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_region_add_argument(struct reduct* reduct,
    reduct_rvsdg_region_t* region);

/**
 * @brief Adds a node to a region.
 *
 * @param region Pointer to the region to add the node to.
 * @param node Pointer to the node to add.
 */
REDUCT_API void reduct_rvsdg_region_add_node(reduct_rvsdg_region_t* region, reduct_rvsdg_node_t* node);

/**
 * @brief Removes a node from its region.
 *
 * @param node Pointer to the node to remove.
 */
REDUCT_API void reduct_rvsdg_region_remove_node(reduct_rvsdg_node_t* node);

/**
 * @brief Removes from the region and disconnects from any connections a node and any nodes connected to its input.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param node Pointer to the node to delete.
 */
REDUCT_API void reduct_rvsdg_node_delete(struct reduct* reduct, reduct_rvsdg_node_t* node);

/**
 * @brief Check if two nodes are structurally identical.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param nodeA First node.
 * @param nodeB Second node.
 * @return true if the nodes are identical, false otherwise.
 */
REDUCT_API bool reduct_rvsdg_node_is_identical(struct reduct* reduct, reduct_rvsdg_node_t* nodeA,
    reduct_rvsdg_node_t* nodeB);

/**
 * @brief Lift an origin from an outer region to an inner region, creating a new argument in the inner region and
 * connecting it to the outer origin.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param region The inner region to lift the origin into.
 * @param outerValue The origin in the outer region to lift.
 * @return The new origin in the inner region representing the lifted value.
 */
REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_region_lift_origin(struct reduct* reduct, reduct_rvsdg_region_t* region,
    reduct_rvsdg_origin_t* outerValue);

/** @} */

#endif
