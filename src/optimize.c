#include "reduct/build.h"
#include "reduct/defs.h"
#include <reduct/core.h>
#include <reduct/dump.h>
#include <reduct/function.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/list.h>
#include <reduct/optimize.h>
#include <reduct/rvsdg.h>

#include <stdio.h>
#include <string.h>

static bool reduct_optimize_constant_folding(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    bool allConstants = true;
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            allConstants = false;
            break;
        }

        reduct_rvsdg_origin_t* origin = input->edge->origin;
        if (origin->ownerKind != REDUCT_RVSDG_OWNER_NODE || origin->node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            allConstants = false;
            break;
        }
    }

    if (!allConstants)
    {
        return false;
    }

    REDUCT_SCRATCH(reduct, args, reduct_handle_t, node->inputCount);
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            REDUCT_SCRATCH_FREE(reduct, args);
            return false;
        }

        args[i] = input->edge->origin->node->constant;
    }

    reduct_native_fn nativeFn = reduct_builder_get_native_fn(node->opcode);
    if (nativeFn == NULL)
    {
        REDUCT_SCRATCH_FREE(reduct, args);
        return false;
    }

    reduct_handle_t result = nativeFn(reduct, node->inputCount, args);
    REDUCT_SCRATCH_FREE(reduct, args);

    reduct_rvsdg_node_t* resultNode = reduct_rvsdg_node_new_simple_constant(reduct, node->parent, result);
    reduct_rvsdg_origin_redirect_users(node->output, resultNode->output);

    return true;
}

static bool reduct_optimize_function_inlining(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    if (!REDUCT_OPCODE_IS_CALL(node->opcode))
    {
        return false;
    }

    reduct_rvsdg_node_t* callable = reduct_rvsdg_node_get_input_node(node, 0);
    if (callable == NULL || callable->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
    {
        return false;
    }

    reduct_rvsdg_region_t* body = callable->firstRegion;
    if (body == NULL)
    {
        return false;
    }

    bool cloned = false;
    if (callable->output->useCount > 1)
    {
        callable = reduct_rvsdg_node_copy(reduct, NULL, callable);
        body = callable->firstRegion;
        cloned = true;
    }

    reduct_rvsdg_node_t* child = body->firstNode;
    while (child != NULL)
    {
        reduct_rvsdg_node_t* next = child->next;
        reduct_rvsdg_region_remove_node(child);
        reduct_rvsdg_region_add_node(node->parent, child);
        child = next;
    }

    reduct_rvsdg_origin_t* arg = body->firstArgument;
    for (uint16_t i = 1; i < node->inputCount; i++)
    {
        reduct_rvsdg_origin_t* res = reduct_rvsdg_node_get_input_origin(node, i);
        if (arg != NULL && res != NULL)
        {
            reduct_rvsdg_origin_redirect_users(arg, res);
        }
        if (arg != NULL)
        {
            arg = arg->next;
        }
    }

    for (uint16_t i = 0; i < callable->inputCount; i++)
    {
        reduct_rvsdg_origin_t* res = reduct_rvsdg_node_get_input_origin(callable, i);
        if (arg != NULL && res != NULL)
        {
            reduct_rvsdg_origin_redirect_users(arg, res);
        }
        if (arg != NULL)
        {
            arg = arg->next;
        }
    }

    if (body->result->edge != NULL)
    {
        reduct_rvsdg_origin_redirect_users(node->output, body->result->edge->origin);
    }

    reduct_rvsdg_node_delete(reduct, node);
    if (cloned)
    {
        reduct_rvsdg_node_delete(reduct, callable);
    }

    return true;
}

static bool reduct_optimize_node(reduct_t* reduct, reduct_rvsdg_node_t* node, reduct_optimize_flags_t flags)
{
    assert(reduct != NULL);
    assert(node != NULL);

    bool changed = false;

    if (node->output->useCount == 0)
    {
        reduct_rvsdg_node_delete(reduct, node);
        return true;
    }

    if (flags & REDUCT_OPTIMIZE_FUNCTION_INLINING && reduct_optimize_function_inlining(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING && reduct_optimize_constant_folding(reduct, node))
    {
        changed = true;
    }

    return changed;
}

static bool reduct_optimize_region(reduct_t* reduct, reduct_rvsdg_region_t* region, reduct_optimize_flags_t flags)
{
    bool changed = false;
    reduct_rvsdg_node_t* prev = NULL;
    reduct_rvsdg_node_t* curr = region->firstNode;
    while (curr != NULL)
    {
        reduct_rvsdg_region_t* sub = curr->firstRegion;
        while (sub != NULL)
        {
            changed |= reduct_optimize_region(reduct, sub, flags);
            sub = sub->next;
        }

        if (reduct_optimize_node(reduct, curr, flags))
        {
            changed = true;
            curr = prev != NULL ? prev->next : region->firstNode;
        }
        else
        {
            prev = curr;
            curr = curr->next;
        }
    }
    return changed;
}

static void reduct_optimize_graph(reduct_t* reduct, reduct_rvsdg_node_t* root, reduct_optimize_flags_t flags)
{
    bool changed = true;
    int iterations = 0;
    const int MAX_ITERATIONS = 16;

    while (changed && iterations < MAX_ITERATIONS)
    {
        changed = false;

        if (root->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA)
        {
            changed |= reduct_optimize_region(reduct, root->firstRegion, flags);
        }

        iterations++;
    }
}

REDUCT_API void reduct_optimize(reduct_t* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags)
{
    assert(reduct != NULL);

    if (flags == REDUCT_OPTIMIZE_NONE)
    {
        return;
    }

    if (!REDUCT_HANDLE_IS_RVSDG_NODE(handle))
    {
        return;
    }

    reduct_rvsdg_node_t* node = REDUCT_HANDLE_TO_RVSDG_NODE(handle);
    reduct_optimize_graph(reduct, node, flags);
}