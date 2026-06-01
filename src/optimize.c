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

REDUCT_API void reduct_optimize_env_init(reduct_optimize_env_t* env)
{
    assert(env != NULL);
    env->lastFlags = REDUCT_OPTIMIZE_NONE;
}

REDUCT_API void reduct_optimize_env_deinit(reduct_optimize_env_t* env)
{
    assert(env != NULL);
    env->lastFlags = REDUCT_OPTIMIZE_NONE;
}

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

    REDUCT_SCRATCH_GET(reduct, args, reduct_handle_t, node->inputCount);
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            REDUCT_SCRATCH_PUT(reduct, args);
            return false;
        }

        args[i] = input->edge->origin->node->constant;
    }

    reduct_native_fn nativeFn = reduct_builder_get_native_fn(node->opcode);
    if (nativeFn == NULL)
    {
        REDUCT_SCRATCH_PUT(reduct, args);
        return false;
    }

    reduct_handle_t result = nativeFn(reduct, node->inputCount, args);
    REDUCT_SCRATCH_PUT(reduct, args);

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

    bool isVariadic = (callable->flags & REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC) != 0;
    uint32_t paramCount = (uint32_t)body->argumentCount - (uint32_t)callable->inputCount;
    uint32_t providedCount = (uint32_t)node->inputCount - 1;

    if (isVariadic)
    {
        if (providedCount < paramCount - 1)
        {
            return false;
        }
    }
    else
    {
        if (providedCount != paramCount)
        {
            return false;
        }
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
    if (!isVariadic)
    {
        for (uint16_t i = 0; i < (uint16_t)paramCount; i++)
        {
            reduct_rvsdg_origin_t* res = reduct_rvsdg_node_get_input_origin(node, (uint16_t)(i + 1));
            if (arg != NULL && res != NULL)
            {
                reduct_rvsdg_origin_redirect_users(arg, res);
            }
            if (arg != NULL)
            {
                arg = arg->next;
            }
        }
    }
    else
    {
        uint16_t fixedCount = (uint16_t)(paramCount - 1);
        for (uint16_t i = 0; i < fixedCount; i++)
        {
            reduct_rvsdg_origin_t* res = reduct_rvsdg_node_get_input_origin(node, (uint16_t)(i + 1));
            if (arg != NULL && res != NULL)
            {
                reduct_rvsdg_origin_redirect_users(arg, res);
            }
            if (arg != NULL)
            {
                arg = arg->next;
            }
        }

        reduct_rvsdg_node_t* listNode = reduct_rvsdg_node_new_simple_opcode(reduct, node->parent, REDUCT_OPCODE_LIST);
        uint16_t remainingCount = (uint16_t)(providedCount - fixedCount);
        for (uint16_t i = 0; i < remainingCount; i++)
        {
            reduct_rvsdg_origin_t* res = reduct_rvsdg_node_get_input_origin(node, (uint16_t)(fixedCount + i + 1));
            reduct_rvsdg_user_t* listIn = reduct_rvsdg_node_add_input(reduct, listNode);
            reduct_rvsdg_edge_connect(reduct, res, listIn);
        }

        if (arg != NULL)
        {
            reduct_rvsdg_origin_redirect_users(arg, listNode->output);
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

static bool reduct_optimize_is_const(reduct_t* reduct, reduct_rvsdg_origin_t* origin, double value)
{
    if (origin == NULL || origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
    {
        return false;
    }

    reduct_rvsdg_node_t* node = origin->node;
    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return false;
    }

    if (!REDUCT_HANDLE_IS_NUMBER_SHAPED(node->constant))
    {
        return false;
    }

    return reduct_handle_as_number(reduct, node->constant) == value;
}

static bool reduct_optimize_algebraic_simplification(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    reduct_rvsdg_origin_t* left = reduct_rvsdg_node_get_input_origin(node, 0);
    reduct_rvsdg_origin_t* right = (node->inputCount > 1) ? reduct_rvsdg_node_get_input_origin(node, 1) : NULL;

    reduct_rvsdg_origin_t* replacement = NULL;

    switch (REDUCT_OPCODE_BASE(node->opcode))
    {
    case REDUCT_OPCODE_MOV: // Should never happen
    {
        replacement = left;
    }
    break;
    case REDUCT_OPCODE_ADD:
    {
        if (reduct_optimize_is_const(reduct, left, 0.0))
        {
            replacement = right;
        }
        else if (reduct_optimize_is_const(reduct, right, 0.0))
        {
            replacement = left;
        }
    }
    break;
    case REDUCT_OPCODE_SUB:
    {
        if (reduct_optimize_is_const(reduct, right, 0.0))
        {
            replacement = left;
        }
        else if (left != NULL && left == right)
        {
            reduct_rvsdg_node_t* zero =
                reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_FROM_NUMBER(0.0));
            replacement = zero->output;
        }
    }
    break;
    case REDUCT_OPCODE_MUL:
    {
        if (reduct_optimize_is_const(reduct, left, 1.0))
        {
            replacement = right;
        }
        else if (reduct_optimize_is_const(reduct, right, 1.0))
        {
            replacement = left;
        }
        else if (reduct_optimize_is_const(reduct, left, 0.0) || reduct_optimize_is_const(reduct, right, 0.0))
        {
            reduct_rvsdg_node_t* zero =
                reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_FROM_NUMBER(0.0));
            replacement = zero->output;
        }
        else if (reduct_optimize_is_const(reduct, right, 2.0))
        {
            reduct_rvsdg_node_t* add =
                reduct_rvsdg_node_new_simple_binary(reduct, node->parent, REDUCT_OPCODE_ADD, left, left);
            replacement = add->output;
        }
        else if (reduct_optimize_is_const(reduct, left, 2.0))
        {
            reduct_rvsdg_node_t* add =
                reduct_rvsdg_node_new_simple_binary(reduct, node->parent, REDUCT_OPCODE_ADD, right, right);
            replacement = add->output;
        }
    }
    break;
    case REDUCT_OPCODE_DIV:
    {
        if (reduct_optimize_is_const(reduct, right, 1.0))
        {
            replacement = left;
        }
    }
    break;
    case REDUCT_OPCODE_BOR:
    {
        if (reduct_optimize_is_const(reduct, left, 0.0))
        {
            replacement = right;
        }
        else if (reduct_optimize_is_const(reduct, right, 0.0))
        {
            replacement = left;
        }
        else if (left != NULL && left == right)
        {
            replacement = left;
        }
    }
    break;
    case REDUCT_OPCODE_BAND:
    {
        if (reduct_optimize_is_const(reduct, left, 0.0) || reduct_optimize_is_const(reduct, right, 0.0))
        {
            reduct_rvsdg_node_t* zero =
                reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_FROM_NUMBER(0.0));
            replacement = zero->output;
        }
        else if (left != NULL && left == right)
        {
            replacement = left;
        }
    }
    break;
    case REDUCT_OPCODE_BXOR:
    {
        if (reduct_optimize_is_const(reduct, left, 0.0))
        {
            replacement = right;
        }
        else if (reduct_optimize_is_const(reduct, right, 0.0))
        {
            replacement = left;
        }
        else if (left != NULL && left == right)
        {
            reduct_rvsdg_node_t* zero =
                reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_FROM_NUMBER(0.0));
            replacement = zero->output;
        }
    }
    break;
    case REDUCT_OPCODE_SHL:
    case REDUCT_OPCODE_SHR:
    {
        if (reduct_optimize_is_const(reduct, right, 0.0))
        {
            replacement = left;
        }
    }
    break;
    case REDUCT_OPCODE_EQ:
    case REDUCT_OPCODE_LE:
    case REDUCT_OPCODE_GE:
    {
        if (left != NULL && left == right)
        {
            replacement = reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_TRUE())->output;
        }
    }
    break;
    case REDUCT_OPCODE_NEQ:
    case REDUCT_OPCODE_LT:
    case REDUCT_OPCODE_GT:
    {
        if (left != NULL && left == right)
        {
            replacement =
                reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_FALSE(reduct))->output;
        }
    }
    break;
    }

    if (replacement != NULL)
    {
        reduct_rvsdg_origin_redirect_users(node->output, replacement);
        reduct_rvsdg_node_delete(reduct, node);
        return true;
    }

    return false;
}

static bool reduct_optimize_cse(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    if (node->parent == NULL)
    {
        return false;
    }

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST && node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    if (node->inputCount > 0)
    {
        reduct_rvsdg_origin_t* origin = reduct_rvsdg_node_get_input_origin(node, 0);
        if (origin != NULL)
        {
            reduct_rvsdg_edge_t* edge = origin->edges;
            while (edge != NULL)
            {
                if (edge->user->ownerKind == REDUCT_RVSDG_OWNER_NODE)
                {
                    reduct_rvsdg_node_t* search = edge->user->node;
                    if (search != node && search->parent == node->parent &&
                        reduct_rvsdg_node_is_identical(reduct, search, node))
                    {
                        reduct_rvsdg_origin_redirect_users(node->output, search->output);
                        reduct_rvsdg_node_delete(reduct, node);
                        return true;
                    }
                }
                edge = edge->next;
            }
            return false;
        }
    }

    reduct_rvsdg_node_t* search = node->parent->firstNode;
    while (search != NULL && search != node)
    {
        if (reduct_rvsdg_node_is_identical(reduct, search, node))
        {
            reduct_rvsdg_origin_redirect_users(node->output, search->output);
            reduct_rvsdg_node_delete(reduct, node);
            return true;
        }
        search = search->next;
    }

    return false;
}

static bool reduct_optimize_gamma_folding(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    if (node->type != REDUCT_RVSDG_NODE_TYPE_GAMMA)
    {
        return false;
    }

    reduct_rvsdg_origin_t* selector = reduct_rvsdg_node_get_input_origin(node, 0);
    if (selector == NULL || selector->ownerKind != REDUCT_RVSDG_OWNER_NODE ||
        selector->node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return false;
    }

    if (!REDUCT_HANDLE_IS_NUMBER_SHAPED(selector->node->constant))
    {
        return false;
    }

    uint32_t index = (uint32_t)reduct_handle_as_number(reduct, selector->node->constant);
    if (index >= node->regionCount)
    {
        return false;
    }

    reduct_rvsdg_region_t* target = node->firstRegion;
    for (uint32_t i = 0; i < index; i++)
    {
        target = target->next;
    }

    reduct_rvsdg_node_t* child = target->firstNode;
    while (child != NULL)
    {
        reduct_rvsdg_node_t* next = child->next;
        reduct_rvsdg_region_remove_node(child);
        reduct_rvsdg_region_add_node(node->parent, child);
        child = next;
    }

    reduct_rvsdg_origin_t* arg = target->firstArgument;
    const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);

    while (arg != NULL)
    {
        uint16_t inputIndex;
        if (reduct_rvsdg_node_map_argument_to_input(node, target, arg->index, &inputIndex))
        {
            reduct_rvsdg_origin_t* inputOrigin = reduct_rvsdg_node_get_input_origin(node, inputIndex);
            if (inputOrigin != NULL)
            {
                reduct_rvsdg_origin_redirect_users(arg, inputOrigin);
            }
        }
        arg = arg->next;
    }

    if (target->result->edge != NULL)
    {
        reduct_rvsdg_origin_redirect_users(node->output, target->result->edge->origin);
    }

    reduct_rvsdg_node_delete(reduct, node);
    return true;
}

static bool reduct_optimize_auto_parallelization(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    uint64_t callAmount = 0;
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            continue;
        }

        reduct_rvsdg_origin_t* origin = input->edge->origin;
        if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE &&
            origin->node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE && REDUCT_OPCODE_IS_CALL(origin->node->opcode) &&
            !REDUCT_OPCODE_IS_FORK(origin->node->opcode))
        {
            callAmount++;
        }
    }

    if (callAmount <= 1)
    {
        return false;
    }

    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            continue;
        }

        reduct_rvsdg_origin_t* origin = input->edge->origin;
        if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE &&
            origin->node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE && REDUCT_OPCODE_IS_CALL(origin->node->opcode) &&
            !REDUCT_OPCODE_IS_FORK(origin->node->opcode))
        {
            reduct_opcode_t mode = origin->node->opcode & REDUCT_OPCODE_MODE_CONST;
            origin->node->opcode = REDUCT_OPCODE_FORK | mode;
        }
    }

    return true;
}

static bool reduct_optimize_node(reduct_t* reduct, reduct_rvsdg_node_t* node, reduct_optimize_flags_t flags)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->output->useCount == 0)
    {
        reduct_rvsdg_node_delete(reduct, node);
        return true;
    }

    bool changed = false;

    if (flags & REDUCT_OPTIMIZE_FUNCTION_INLINING && reduct_optimize_function_inlining(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING && reduct_optimize_constant_folding(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_ALGEBRAIC_SIMPLIFICATION && reduct_optimize_algebraic_simplification(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_GAMMA_FOLDING && reduct_optimize_gamma_folding(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_CSE && reduct_optimize_cse(reduct, node))
    {
        changed = true;
    }

    /*if (flags & REDUCT_OPTIMIZE_AUTO_PARALLELIZATION && reduct_optimize_auto_parallelization(reduct, node))
    {
        changed = true;
    }*/

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
    uint32_t iterations = 0;
    const uint32_t MAX_ITERATIONS = 16;

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
    reduct->env->optimize.lastFlags = flags;
}