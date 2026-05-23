#include "reduct/optimize.h"
#include "reduct/core.h"
#include "reduct/function.h"
#include "reduct/handle.h"
#include "reduct/inst.h"
#include "reduct/list.h"

#include <string.h>

static reduct_optimize_edge_t* reduct_optimize_edge_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_OPTIMIZE_EDGE;
    reduct_optimize_edge_t* edge = &item->optimizationEdge;
    memset(edge, 0, sizeof(reduct_optimize_edge_t));
    return edge;
}

static void reduct_optimize_edge_connect(reduct_t* reduct, reduct_optimize_origin_t* origin, reduct_optimize_user_t* user)
{
    reduct_optimize_edge_t* edge = reduct_optimize_edge_new(reduct);
    edge->origin = origin;
    edge->user = user;

    edge->next = origin->uses;
    edge->prev = NULL;
    if (origin->uses)
    {
        origin->uses->prev = edge;
    }
    origin->uses = edge;
    origin->useCount++;

    user->use = edge;
}

static reduct_optimize_node_t* reduct_optimize_node_new(reduct_t* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_OPTIMIZE_NODE;
    reduct_optimize_node_t* node = &item->optimizationNode;
    memset(node, 0, sizeof(reduct_optimize_node_t));
    return node;
}

static void reduct_optimize_node_create_inputs(reduct_t* reduct, reduct_optimize_node_t* node, size_t count)
{
    node->inputCount = count;
    node->inputs = (reduct_optimize_user_t*)calloc(count, sizeof(reduct_optimize_user_t));
    if (node->inputs == NULL)
    {
        REDUCT_ERROR_THROW(reduct, "out of memory");
    }
}

static void reduct_optimize_node_create_outputs(reduct_t* reduct, reduct_optimize_node_t* node, size_t count)
{
    node->outputCount = count;
    node->outputs = (reduct_optimize_origin_t*)calloc(count, sizeof(reduct_optimize_origin_t));
    if (node->outputs == NULL)
    {
        REDUCT_ERROR_THROW(reduct, "out of memory");
    }
}

static void reduct_optimize_node_create_regions(reduct_t* reduct, reduct_optimize_node_t* node, size_t count)
{
    node->regionCount = count;
    node->regions = (reduct_optimize_region_t*)calloc(count, sizeof(reduct_optimize_region_t));
    if (node->regions == NULL)
    {
        REDUCT_ERROR_THROW(reduct, "out of memory");
    }
}

static reduct_optimize_node_t* reduct_optimize_node_new_simple_constant(reduct_t* reduct, reduct_const_t constant)
{
    reduct_optimize_node_t* node = reduct_optimize_node_new(reduct);
    node->type = REDUCT_OPTIMIZE_NODE_TYPE_SIMPLE_CONST;
    node->constant = constant;
    reduct_optimize_node_create_outputs(reduct, node, 1);
    return node;
}

static reduct_optimize_node_t* reduct_optimize_node_new_lambda(reduct_t* reduct, size_t arity)
{    
    reduct_optimize_node_t* node = reduct_optimize_node_new(reduct);
    node->type = REDUCT_OPTIMIZE_NODE_TYPE_LAMBDA;
    reduct_optimize_node_create_inputs(reduct, node, arity);
    reduct_optimize_node_create_outputs(reduct, node, 1);
    reduct_optimize_node_create_regions(reduct, node, 1);
    return node;
}

static void reduct_optimize_graph_construct(reduct_t* reduct, reduct_optimize_graph_t* graph, reduct_function_t* func)
{
    reduct_optimize_node_t* lambda = reduct_optimize_node_new_lambda(reduct, func->arity);

    reduct_optimize_origin_t* constants[REDUCT_CONSTANT_MAX] = {0};
    for (size_t i = 0; i < func->constantCount; i++)
    {
        reduct_optimize_node_t* node = reduct_optimize_node_new_simple_constant(reduct, i);
        constants[i] = &node->outputs[0];
    }
    
    for (size_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);

    }
}

REDUCT_API void reduct_optimize(reduct_t* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        return;
    }

    reduct_function_t* func = REDUCT_HANDLE_TO_FUNCTION(handle);
    if (func->instCount == 0)
    {
        return;
    }

    if (func->flags & REDUCT_FUNCTION_FLAG_OPTIMIZED)
    {
        return;
    }
    func->flags |= REDUCT_FUNCTION_FLAG_OPTIMIZED;

    reduct_optimize_graph_t graph = {0};
    reduct_optimize_graph_construct(reduct, &graph, func);

    for (size_t i = 0; i < func->constantCount; i++)
    {
        reduct_const_slot_t* slot = &func->constants[i];
        if (slot->type == REDUCT_CONST_SLOT_TYPE_HANDLE && REDUCT_HANDLE_IS_FUNCTION(slot->handle))
        {
            reduct_optimize(reduct, slot->handle, flags);
        }
    }

    func->optimizeFlags = flags;
}