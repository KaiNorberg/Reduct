#include <reduct/core.h>
#include <reduct/dump.h>
#include <reduct/function.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/list.h>
#include <reduct/rvsdg.h>

#include <stdio.h>
#include <string.h>

REDUCT_API reduct_rvsdg_edge_t* reduct_rvsdg_edge_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_EDGE;
    reduct_rvsdg_edge_t* edge = &item->rvsdgEdge;
    memset(edge, 0, sizeof(reduct_rvsdg_edge_t));
    return edge;
}

REDUCT_API void reduct_rvsdg_edge_connect(reduct_t* reduct, reduct_rvsdg_origin_t* origin, reduct_rvsdg_user_t* user)
{
    reduct_rvsdg_edge_t* edge = reduct_rvsdg_edge_new(reduct);
    edge->origin = origin;
    edge->user = user;

    edge->next = origin->edges;
    edge->prev = NULL;
    if (origin->edges != NULL)
    {
        origin->edges->prev = edge;
    }
    origin->edges = edge;
    origin->useCount++;

    user->edge = edge;
}

REDUCT_API void reduct_rvsdg_edge_disconnect(reduct_rvsdg_edge_t* edge)
{
    if (edge == NULL || edge->origin == NULL)
    {
        return;
    }

    if (edge->prev != NULL)
    {
        edge->prev->next = edge->next;
    }
    else
    {
        edge->origin->edges = edge->next;
    }
    if (edge->next != NULL)
    {
        edge->next->prev = edge->prev;
    }
    edge->origin->useCount--;

    if (edge->user != NULL)
    {
        edge->user->edge = NULL;
    }
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_NODE;
    reduct_rvsdg_node_t* node = &item->rvsdgNode;
    memset(node, 0, sizeof(reduct_rvsdg_node_t));
    node->output = reduct_rvsdg_origin_new(reduct);
    node->output->ownerKind = REDUCT_RVSDG_OWNER_NODE;
    node->output->node = node;
    return node;
}

REDUCT_API reduct_rvsdg_region_t* reduct_rvsdg_region_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_REGION;
    reduct_rvsdg_region_t* region = &item->rvsdgRegion;
    memset(region, 0, sizeof(reduct_rvsdg_region_t));
    return region;
}

REDUCT_API reduct_rvsdg_user_t* reduct_rvsdg_user_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_USER;
    reduct_rvsdg_user_t* user = &item->rvsdgUser;
    memset(user, 0, sizeof(reduct_rvsdg_user_t));
    return user;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_origin_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_ORIGIN;
    reduct_rvsdg_origin_t* origin = &item->rvsdgOrigin;
    memset(origin, 0, sizeof(reduct_rvsdg_origin_t));
    return origin;
}

REDUCT_API reduct_rvsdg_user_t* reduct_rvsdg_node_add_input(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    reduct_rvsdg_user_t* user = reduct_rvsdg_user_new(reduct);
    user->ownerKind = REDUCT_RVSDG_OWNER_NODE;
    user->node = node;
    user->index = node->inputCount++;
    user->next = NULL;
    if (node->firstInput == NULL)
    {
        node->firstInput = user;
    }
    else
    {
        reduct_rvsdg_user_t* curr = node->firstInput;
        while (curr->next != NULL)
        {
            curr = curr->next;
        }
        curr->next = user;
    }
    return user;
}

REDUCT_API reduct_rvsdg_region_t* reduct_rvsdg_node_add_region(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    reduct_rvsdg_region_t* region = reduct_rvsdg_region_new(reduct);
    region->parent = node;
    region->next = NULL;
    region->result = reduct_rvsdg_user_new(reduct);
    region->result->ownerKind = REDUCT_RVSDG_OWNER_REGION;
    region->result->region = region;
    region->result->index = 0;

    if (node->firstRegion == NULL)
    {
        node->firstRegion = region;
    }
    else
    {
        reduct_rvsdg_region_t* curr = node->firstRegion;
        while (curr->next != NULL)
        {
            curr = curr->next;
        }
        curr->next = region;
    }

    node->regionCount++;
    return region;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_region_add_argument(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_origin_t* origin = reduct_rvsdg_origin_new(reduct);
    origin->ownerKind = REDUCT_RVSDG_OWNER_REGION;
    origin->region = region;
    origin->index = region->argumentCount++;
    origin->next = NULL;
    if (region->firstArgument == NULL)
    {
        region->firstArgument = origin;
    }
    else
    {
        reduct_rvsdg_origin_t* curr = region->firstArgument;
        while (curr->next != NULL)
        {
            curr = curr->next;
        }
        curr->next = origin;
    }
    return origin;
}

REDUCT_API void reduct_rvsdg_region_add_node(reduct_rvsdg_region_t* region, reduct_rvsdg_node_t* node)
{
    node->parent = region;
    node->next = NULL;

    if (region->lastNode != NULL)
    {
        region->lastNode->next = node;
    }
    else
    {
        region->firstNode = node;
    }
    region->lastNode = node;
}

REDUCT_API void reduct_rvsdg_region_remove_node(reduct_rvsdg_node_t* node)
{
    reduct_rvsdg_region_t* region = node->parent;
    if (region == NULL)
    {
        return;
    }

    reduct_rvsdg_node_t* prev = NULL;
    reduct_rvsdg_node_t* curr = region->firstNode;

    while (curr != NULL && curr != node)
    {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL)
    {
        return;
    }

    if (prev != NULL)
    {
        prev->next = node->next;
    }
    else
    {
        region->firstNode = node->next;
    }

    if (node->next == NULL)
    {
        region->lastNode = prev;
    }

    node->next = NULL;
    node->parent = NULL;
}

REDUCT_API void reduct_rvsdg_node_delete(struct reduct* reduct, reduct_rvsdg_node_t* node)
{
    if (node == NULL)
    {
        return;
    }

    reduct_rvsdg_region_t* region = node->firstRegion;
    while (region != NULL)
    {
        reduct_rvsdg_node_t* curr = region->firstNode;
        while (curr != NULL)
        {
            reduct_rvsdg_node_t* next = curr->next;
            reduct_rvsdg_node_delete(reduct, curr);
            curr = next;
        }

        if (region->result != NULL)
        {
            reduct_rvsdg_edge_disconnect(region->result->edge);
        }

        reduct_rvsdg_origin_t* arg = region->firstArgument;
        while (arg != NULL)
        {
            reduct_rvsdg_edge_t* edge = arg->edges;
            while (edge != NULL)
            {
                reduct_rvsdg_edge_t* next_edge = edge->next;
                reduct_rvsdg_edge_disconnect(edge);
                edge = next_edge;
            }
            arg = arg->next;
        }

        region = region->next;
    }

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        reduct_rvsdg_edge_t* edge = input->edge;
        if (edge != NULL && edge->origin->ownerKind == REDUCT_RVSDG_OWNER_NODE && edge->origin->node != node)
        {
            reduct_rvsdg_origin_t* origin = edge->origin;
            reduct_rvsdg_edge_disconnect(edge);

            if (origin->useCount == 0)
            {
                reduct_rvsdg_node_delete(reduct, origin->node);
            }
        }
        else
        {
            reduct_rvsdg_edge_disconnect(input->edge);
        }
        input = input->next;
    }

    reduct_rvsdg_origin_t* output = node->output;
    while (output != NULL)
    {
        reduct_rvsdg_edge_t* edge = output->edges;
        while (edge != NULL)
        {
            reduct_rvsdg_edge_disconnect(edge);
            edge = output->edges;
        }
        output = output->next;
    }

    if (node->parent != NULL)
    {
        reduct_rvsdg_region_remove_node(node);
    }
}

REDUCT_API bool reduct_rvsdg_node_is_identical(struct reduct* reduct, reduct_rvsdg_node_t* nodeA,
    reduct_rvsdg_node_t* nodeB)
{
    if (nodeA == nodeB)
    {
        return true;
    }

    if (nodeA->type != nodeB->type || nodeA->inputCount != nodeB->inputCount || nodeA->flags != nodeB->flags)
    {
        return false;
    }

    if (nodeA->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        if (nodeA->opcode != nodeB->opcode)
        {
            return false;
        }
    }
    else if (nodeA->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        if (!reduct_handle_is_equal(reduct, nodeA->constant, nodeB->constant))
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    for (uint16_t i = 0; i < nodeA->inputCount; i++)
    {
        if (reduct_rvsdg_node_get_input_origin(nodeA, i) != reduct_rvsdg_node_get_input_origin(nodeB, i))
        {
            return false;
        }
    }

    return true;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_opcode(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_opcode_t opcode)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE;
    node->opcode = REDUCT_OPCODE_BASE(opcode);
    if (region != NULL)
    {
        reduct_rvsdg_region_add_node(region, node);
    }
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_constant(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_handle_t constant)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST;
    node->constant = constant;
    if (region != NULL)
    {
        reduct_rvsdg_region_add_node(region, node);
    }
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_unary(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_opcode_t opcode, reduct_rvsdg_origin_t* input)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new_simple_opcode(reduct, region, opcode);
    reduct_rvsdg_edge_connect(reduct, input, reduct_rvsdg_node_add_input(reduct, node));
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_binary(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_opcode_t opcode, reduct_rvsdg_origin_t* left, reduct_rvsdg_origin_t* right)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new_simple_opcode(reduct, region, opcode);
    reduct_rvsdg_edge_connect(reduct, left, reduct_rvsdg_node_add_input(reduct, node));
    reduct_rvsdg_edge_connect(reduct, right, reduct_rvsdg_node_add_input(reduct, node));
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_ternary(struct reduct* reduct,
    reduct_rvsdg_region_t* region, reduct_opcode_t opcode, struct reduct_rvsdg_origin* a, struct reduct_rvsdg_origin* b,
    struct reduct_rvsdg_origin* c)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new_simple_opcode(reduct, region, opcode);
    reduct_rvsdg_edge_connect(reduct, a, reduct_rvsdg_node_add_input(reduct, node));
    reduct_rvsdg_edge_connect(reduct, b, reduct_rvsdg_node_add_input(reduct, node));
    reduct_rvsdg_edge_connect(reduct, c, reduct_rvsdg_node_add_input(reduct, node));
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_lambda(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_LAMBDA;
    reduct_rvsdg_node_add_region(reduct, node);

    if (region != NULL)
    {
        reduct_rvsdg_region_add_node(region, node);
    }
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_phi(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_PHI;
    reduct_rvsdg_node_add_region(reduct, node);

    if (region != NULL)
    {
        reduct_rvsdg_region_add_node(region, node);
    }
    return node;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_gamma(reduct_t* reduct, reduct_rvsdg_region_t* region,
    uint8_t regionCount)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_GAMMA;

    reduct_rvsdg_node_add_input(reduct, node);

    for (uint8_t i = 0; i < regionCount; i++)
    {
        reduct_rvsdg_node_add_region(reduct, node);
    }

    if (region != NULL)
    {
        reduct_rvsdg_region_add_node(region, node);
    }
    return node;
}

static const reduct_rvsdg_node_info_t rvsdgNodeInfoTable[] = {
    [REDUCT_RVSDG_NODE_TYPE_INVALID] = {"INVALID", "#ffffff", 0},
    [REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE] = {"OPCODE", "#FFFF82", 0},
    [REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST] = {"CONST", "#FFFF82", 0},
    [REDUCT_RVSDG_NODE_TYPE_GAMMA] = {"GAMMA", "#80FF80", 1},
    [REDUCT_RVSDG_NODE_TYPE_LAMBDA] = {"LAMBDA", "#80B3FF", 0},
    [REDUCT_RVSDG_NODE_TYPE_PHI] = {"PHI", "#FFB380", 0},
};

REDUCT_API const reduct_rvsdg_node_info_t* reduct_rvsdg_node_get_info(reduct_rvsdg_node_type_t type)
{
    if (REDUCT_UNLIKELY(type >= sizeof(rvsdgNodeInfoTable) / sizeof(reduct_rvsdg_node_info_t)))
    {
        return &rvsdgNodeInfoTable[REDUCT_RVSDG_NODE_TYPE_INVALID];
    }
    return &rvsdgNodeInfoTable[type];
}

REDUCT_API struct reduct_rvsdg_user* reduct_rvsdg_node_get_input(reduct_rvsdg_node_t* node, uint16_t index)
{
    reduct_rvsdg_user_t* curr = node->firstInput;
    while (curr != NULL)
    {
        if (curr->index == index)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

REDUCT_API struct reduct_rvsdg_origin* reduct_rvsdg_node_get_input_origin(reduct_rvsdg_node_t* node, uint16_t index)
{
    reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, index);
    if (input != NULL && input->edge != NULL)
    {
        return input->edge->origin;
    }
    return NULL;
}

REDUCT_API struct reduct_rvsdg_node* reduct_rvsdg_node_get_input_node(reduct_rvsdg_node_t* node, uint16_t index)
{
    reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, index);
    if (input != NULL && input->edge != NULL)
    {
        reduct_rvsdg_origin_t* origin = input->edge->origin;
        if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE)
        {
            return origin->node;
        }
    }
    return NULL;
}

REDUCT_API void reduct_rvsdg_origin_redirect_users(struct reduct_rvsdg_origin* origin,
    struct reduct_rvsdg_origin* newOrigin)
{
    if (origin == newOrigin)
    {
        return;
    }

    reduct_rvsdg_edge_t* edge = origin->edges;
    while (edge != NULL)
    {
        reduct_rvsdg_edge_t* next = edge->next;
        reduct_rvsdg_edge_redirect(edge, newOrigin);
        edge = next;
    }
}

REDUCT_API struct reduct_rvsdg_origin* reduct_rvsdg_region_get_argument(reduct_rvsdg_region_t* region, uint16_t index)
{
    reduct_rvsdg_origin_t* curr = region->firstArgument;
    while (curr != NULL)
    {
        if (curr->index == index)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

REDUCT_API bool reduct_rvsdg_region_is_ancestor_or_same(reduct_rvsdg_region_t* region, reduct_rvsdg_region_t* ancestor)
{
    while (region != NULL)
    {
        if (region == ancestor)
        {
            return true;
        }
        if (region->parent == NULL)
        {
            break;
        }
        region = region->parent->parent;
    }
    return false;
}

REDUCT_API void reduct_rvsdg_edge_redirect(reduct_rvsdg_edge_t* edge, reduct_rvsdg_origin_t* newOrigin)
{
    if (edge->prev != NULL)
    {
        edge->prev->next = edge->next;
    }
    else
    {
        edge->origin->edges = edge->next;
    }
    if (edge->next != NULL)
    {
        edge->next->prev = edge->prev;
    }
    edge->origin->useCount--;

    edge->origin = newOrigin;
    edge->next = newOrigin->edges;
    edge->prev = NULL;
    if (newOrigin->edges)
    {
        newOrigin->edges->prev = edge;
    }
    newOrigin->edges = edge;
    newOrigin->useCount++;
}

static inline reduct_rvsdg_origin_t* reduct_rvsdg_get_redirection_origin(reduct_rvsdg_user_t* user,
    reduct_rvsdg_region_t* inner, reduct_rvsdg_origin_t* recurArg, reduct_rvsdg_origin_t* phiOut)
{
    reduct_rvsdg_region_t* userRegion =
        (user->ownerKind == REDUCT_RVSDG_OWNER_NODE) ? user->node->parent : user->region;

    bool internal = reduct_rvsdg_region_is_ancestor_or_same(userRegion, inner);
    return internal ? recurArg : phiOut;
}

REDUCT_API void reduct_rvsdg_node_phi_wrap_lambda(reduct_t* reduct, reduct_rvsdg_node_t* lambda)
{
    if (lambda->parent == NULL || lambda->parent->parent == NULL)
    {
        return;
    }
    if (lambda->parent->parent->type == REDUCT_RVSDG_NODE_TYPE_PHI)
    {
        return;
    }

    reduct_rvsdg_region_t* parentRegion = lambda->parent;
    reduct_rvsdg_node_t* phi = reduct_rvsdg_node_new_phi(reduct, parentRegion);

    reduct_rvsdg_region_remove_node(lambda);
    reduct_rvsdg_region_add_node(phi->firstRegion, lambda);

    reduct_rvsdg_user_t* res = phi->firstRegion->result;
    reduct_rvsdg_edge_connect(reduct, lambda->output, res);

    reduct_rvsdg_origin_t* recurArg = reduct_rvsdg_region_add_argument(reduct, phi->firstRegion);

    reduct_rvsdg_user_t* in = lambda->firstInput;
    while (in != NULL)
    {
        reduct_rvsdg_origin_t* extOrig = in->edge->origin;

        reduct_rvsdg_user_t* phiIn = reduct_rvsdg_node_add_input(reduct, phi);
        reduct_rvsdg_edge_connect(reduct, extOrig, phiIn);

        reduct_rvsdg_origin_t* phiArg = reduct_rvsdg_region_add_argument(reduct, phi->firstRegion);
        reduct_rvsdg_edge_redirect(in->edge, phiArg);

        in = in->next;
    }

    reduct_rvsdg_origin_t* lambdaOut = lambda->output;
    reduct_rvsdg_origin_t* phiOut = phi->output;

    reduct_rvsdg_edge_t* edge = lambdaOut->edges;
    while (edge != NULL)
    {
        reduct_rvsdg_edge_t* next = edge->next;
        if (edge->user != res)
        {
            reduct_rvsdg_origin_t* newOrigin =
                reduct_rvsdg_get_redirection_origin(edge->user, phi->firstRegion, recurArg, phiOut);
            reduct_rvsdg_edge_redirect(edge, newOrigin);
        }
        edge = next;
    }
}

REDUCT_API bool reduct_rvsdg_node_is_inside_phi(reduct_rvsdg_node_t* node)
{
    return node->parent != NULL && node->parent->parent != NULL &&
        node->parent->parent->type == REDUCT_RVSDG_NODE_TYPE_PHI;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_call(struct reduct* reduct, reduct_rvsdg_region_t* region,
    reduct_rvsdg_origin_t* callable)
{
    reduct_rvsdg_node_t* call = reduct_rvsdg_node_new_simple_opcode(reduct, region, REDUCT_OPCODE_CALL);
    reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(reduct, call);
    reduct_rvsdg_edge_connect(reduct, callable, input);
    return call;
}

REDUCT_API bool reduct_rvsdg_node_map_input_to_argument(reduct_rvsdg_node_t* node, reduct_rvsdg_region_t* region,
    uint16_t inputIndex, uint16_t* outArgIndex)
{
    assert(node != NULL);
    assert(region != NULL);
    assert(region->parent == node);

    switch (node->type)
    {
    case REDUCT_RVSDG_NODE_TYPE_GAMMA:
    {
        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);
        if (inputIndex < info->dataInputOffset)
        {
            return false;
        }
        *outArgIndex = (uint16_t)(inputIndex - info->dataInputOffset);
        return true;
    }
    case REDUCT_RVSDG_NODE_TYPE_LAMBDA:
    case REDUCT_RVSDG_NODE_TYPE_PHI:
    {
        *outArgIndex = (uint16_t)((region->argumentCount - node->inputCount) + inputIndex);
        return true;
    }
    default:
        return false;
    }
}

REDUCT_API bool reduct_rvsdg_node_map_argument_to_input(reduct_rvsdg_node_t* node, reduct_rvsdg_region_t* region,
    uint16_t argIndex, uint16_t* outInputIndex)
{
    assert(node != NULL);
    assert(region != NULL);
    assert(region->parent == node);

    switch (node->type)
    {
    case REDUCT_RVSDG_NODE_TYPE_GAMMA:
    {
        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);
        *outInputIndex = (uint16_t)(argIndex + info->dataInputOffset);
        return true;
    }
    case REDUCT_RVSDG_NODE_TYPE_LAMBDA:
    case REDUCT_RVSDG_NODE_TYPE_PHI:
    {
        uint16_t base = (uint16_t)(region->argumentCount - node->inputCount);
        if (argIndex >= base)
        {
            *outInputIndex = (uint16_t)(argIndex - base);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

REDUCT_API bool reduct_rvsdg_node_is_recur_origin(reduct_rvsdg_node_t* node, reduct_rvsdg_origin_t* origin)
{
    if (node->type != REDUCT_RVSDG_NODE_TYPE_PHI || origin->ownerKind != REDUCT_RVSDG_OWNER_REGION)
    {
        return false;
    }

    reduct_rvsdg_region_t* region = origin->region;
    if (region->parent != node)
    {
        return false;
    }

    uint32_t recurSlots = (uint32_t)region->argumentCount - (uint32_t)node->inputCount;
    return origin->index < recurSlots;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_region_lift_origin(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_rvsdg_origin_t* outerValue)
{
    assert(outerValue != NULL);

    if (region != NULL && region->parent != NULL)
    {
        reduct_rvsdg_node_t* node = region->parent;

        if (node->type == REDUCT_RVSDG_NODE_TYPE_PHI && outerValue->ownerKind == REDUCT_RVSDG_OWNER_NODE &&
            outerValue->node == node)
        {
            return reduct_rvsdg_region_get_argument(region, outerValue->index);
        }

        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);
        reduct_rvsdg_user_t* input = node->firstInput;
        while (input != NULL)
        {
            if (input->edge != NULL && input->edge->origin == outerValue)
            {
                if (input->index >= info->dataInputOffset)
                {
                    break;
                }
            }
            input = input->next;
        }

        reduct_rvsdg_origin_t* resultArg;
        if (input == NULL)
        {
            input = reduct_rvsdg_node_add_input(reduct, node);
            reduct_rvsdg_edge_connect(reduct, outerValue, input);

            resultArg = NULL;
            reduct_rvsdg_region_t* current = node->firstRegion;
            while (current != NULL)
            {
                reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_add_argument(reduct, current);
                if (current == region)
                {
                    resultArg = arg;
                }
                current = current->next;
            }
        }
        else
        {
            uint16_t argIndex = 0;
            bool mapped = reduct_rvsdg_node_map_input_to_argument(node, region, (uint16_t)input->index, &argIndex);
            assert(mapped);
            resultArg = reduct_rvsdg_region_get_argument(region, argIndex);
        }

        return resultArg;
    }

    return outerValue;
}
