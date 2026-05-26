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

    edge->next = origin->uses;
    edge->prev = NULL;
    if (origin->uses != NULL)
    {
        origin->uses->prev = edge;
    }
    origin->uses = edge;
    origin->useCount++;

    user->use = edge;
}

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_RVSDG_NODE;
    reduct_rvsdg_node_t* node = &item->rvsdgNode;
    memset(node, 0, sizeof(reduct_rvsdg_node_t));
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
    user->next = node->firstInput;
    node->firstInput = user;
    return user;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_node_add_output(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    reduct_rvsdg_origin_t* origin = reduct_rvsdg_origin_new(reduct);
    origin->ownerKind = REDUCT_RVSDG_OWNER_NODE;
    origin->node = node;
    origin->index = node->outputCount++;
    origin->next = node->firstOutput;
    node->firstOutput = origin;
    return origin;
}

REDUCT_API reduct_rvsdg_region_t* reduct_rvsdg_node_add_region(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    reduct_rvsdg_region_t* region = reduct_rvsdg_region_new(reduct);
    region->parent = node;
    region->next = node->firstRegion;
    node->firstRegion = region;
    node->regionCount++;
    return region;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_rvsdg_region_add_argument(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_origin_t* origin = reduct_rvsdg_origin_new(reduct);
    origin->ownerKind = REDUCT_RVSDG_OWNER_REGION;
    origin->region = region;
    origin->index = region->argumentCount++;
    origin->next = region->firstArgument;
    region->firstArgument = origin;
    return origin;
}

REDUCT_API reduct_rvsdg_user_t* reduct_rvsdg_region_add_result(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_user_t* user = reduct_rvsdg_user_new(reduct);
    user->ownerKind = REDUCT_RVSDG_OWNER_REGION;
    user->region = region;
    user->index = region->resultCount++;
    user->next = region->firstResult;
    region->firstResult = user;
    return user;
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

REDUCT_API void reduct_rvsdg_region_remove_node(reduct_rvsdg_region_t* region, reduct_rvsdg_node_t* node)
{
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

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_simple_opcode(reduct_t* reduct, reduct_rvsdg_region_t* region,
    reduct_opcode_t opcode)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE;
    node->opcode = REDUCT_OPCODE_BASE(opcode);
    reduct_rvsdg_node_add_output(reduct, node);
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
    reduct_rvsdg_node_add_output(reduct, node);
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

REDUCT_API reduct_rvsdg_node_t* reduct_rvsdg_node_new_lambda(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new(reduct);
    node->type = REDUCT_RVSDG_NODE_TYPE_LAMBDA;
    reduct_rvsdg_node_add_output(reduct, node);
    reduct_rvsdg_region_t* inner = reduct_rvsdg_node_add_region(reduct, node);
    reduct_rvsdg_region_add_result(reduct, inner);

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
    reduct_rvsdg_node_add_output(reduct, node);

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
    reduct_rvsdg_node_add_output(reduct, node);

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
    if (REDUCT_UNLIKELY(
            type >= sizeof(rvsdgNodeInfoTable) / sizeof(reduct_rvsdg_node_info_t)))
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

REDUCT_API struct reduct_rvsdg_origin* reduct_rvsdg_node_get_output(reduct_rvsdg_node_t* node, uint16_t index)
{
    reduct_rvsdg_origin_t* curr = node->firstOutput;
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

REDUCT_API struct reduct_rvsdg_user* reduct_rvsdg_region_get_result(reduct_rvsdg_region_t* region, uint16_t index)
{
    reduct_rvsdg_user_t* curr = region->firstResult;
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
    if (edge->prev)
    {
        edge->prev->next = edge->next;
    }
    else
    {
        edge->origin->uses = edge->next;
    }
    if (edge->next)
    {
        edge->next->prev = edge->prev;
    }
    edge->origin->useCount--;

    edge->origin = newOrigin;
    edge->next = newOrigin->uses;
    edge->prev = NULL;
    if (newOrigin->uses)
    {
        newOrigin->uses->prev = edge;
    }
    newOrigin->uses = edge;
    newOrigin->useCount++;
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

    reduct_rvsdg_region_remove_node(parentRegion, lambda);
    reduct_rvsdg_region_add_node(phi->firstRegion, lambda);

    reduct_rvsdg_user_t* res = reduct_rvsdg_region_add_result(reduct, phi->firstRegion);
    reduct_rvsdg_edge_connect(reduct, lambda->firstOutput, res);

    reduct_rvsdg_origin_t* recurArg = reduct_rvsdg_region_add_argument(reduct, phi->firstRegion);

    reduct_rvsdg_user_t* in = lambda->firstInput;
    while (in != NULL)
    {
        reduct_rvsdg_origin_t* extOrig = in->use->origin;

        reduct_rvsdg_user_t* phiIn = reduct_rvsdg_node_add_input(reduct, phi);
        reduct_rvsdg_edge_connect(reduct, extOrig, phiIn);

        reduct_rvsdg_origin_t* phiArg = reduct_rvsdg_region_add_argument(reduct, phi->firstRegion);
        reduct_rvsdg_edge_redirect(in->use, phiArg);

        in = in->next;
    }

    reduct_rvsdg_origin_t* lambdaOut = lambda->firstOutput;
    reduct_rvsdg_origin_t* phiOut = phi->firstOutput;

    reduct_rvsdg_edge_t* edge = lambdaOut->uses;
    while (edge != NULL)
    {
        reduct_rvsdg_edge_t* next = edge->next;
        if (edge->user != res)
        {
            reduct_rvsdg_region_t* userRegion =
                (edge->user->ownerKind == REDUCT_RVSDG_OWNER_NODE) ? edge->user->node->parent : edge->user->region;

            bool internal = reduct_rvsdg_region_is_ancestor_or_same(userRegion, phi->firstRegion);

            reduct_rvsdg_origin_t* newOrigin = internal ? recurArg : phiOut;
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