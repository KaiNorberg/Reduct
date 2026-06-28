#include <reduct/build.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/dump.h>
#include <reduct/function.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/list.h>
#include <reduct/optimize.h>
#include <reduct/rvsdg.h>

#include <stdio.h>
#include <string.h>

REDUCT_API void reduct_optimize_global_init(reduct_optimize_global_t* global)
{
    assert(global != NULL);
    global->lastFlags = REDUCT_OPTIMIZE_NONE;
}

REDUCT_API void reduct_optimize_global_deinit(reduct_optimize_global_t* global)
{
    assert(global != NULL);
    global->lastFlags = REDUCT_OPTIMIZE_NONE;
}

static reduct_rvsdg_origin_t* reduct_optimize_localize_origin(reduct_t* reduct, reduct_rvsdg_region_t* targetRegion,
    reduct_rvsdg_origin_t* remoteOrigin)
{
    if (remoteOrigin == NULL)
    {
        return NULL;
    }

    reduct_rvsdg_region_t* originRegion = NULL;
    if (remoteOrigin->ownerKind == REDUCT_RVSDG_OWNER_NODE)
    {
        originRegion = remoteOrigin->node->parent;
    }
    else if (remoteOrigin->ownerKind == REDUCT_RVSDG_OWNER_REGION)
    {
        originRegion = remoteOrigin->region;
    }

    if (originRegion == targetRegion)
    {
        return remoteOrigin;
    }

    if (remoteOrigin->ownerKind == REDUCT_RVSDG_OWNER_NODE &&
        remoteOrigin->node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        reduct_rvsdg_node_t* localConst =
            reduct_rvsdg_node_new_simple_constant(reduct, targetRegion, remoteOrigin->node->constant);
        return localConst->output;
    }

    return reduct_rvsdg_region_lift_origin(reduct, targetRegion, remoteOrigin);
}

static bool reduct_optimize_node_is_pure(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        if (!REDUCT_HANDLE_IS_ATOM_LIKE(node->constant))
        {
            return true;
        }

        reduct_atom_t* atom = reduct_handle_as_atom(reduct, node->constant);
        if (!reduct_atom_is_native(reduct, atom))
        {
            return true;
        }

        if (atom->length > 0 && atom->string[atom->length - 1] == '!')
        {
            return false;
        }

        return true;
    }

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return true;
    }

    if (REDUCT_OPCODE_IS_CALL(node->opcode))
    {
        reduct_rvsdg_node_t* callable = reduct_rvsdg_node_get_input_node(node, 0);
        if (callable == NULL || callable->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            return true;
        }

        return reduct_optimize_node_is_pure(reduct, callable);
    }

    return true;
}

static bool reduct_optimize_constant_folding(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (!reduct_optimize_node_is_pure(reduct, node))
    {
        return false;
    }

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

        reduct_rvsdg_origin_t* resolved = reduct_rvsdg_resolve_origin(input->edge->origin);

        if (resolved == NULL || resolved->ownerKind != REDUCT_RVSDG_OWNER_NODE ||
            resolved->node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
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
        reduct_rvsdg_origin_t* resolved = reduct_rvsdg_resolve_origin(input->edge->origin);
        args[i] = resolved->node->constant;
    }

    reduct_handle_t result;
    if (REDUCT_OPCODE_IS_CALL(node->opcode) && node->inputCount > 0)
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, args[0]);
        if (!reduct_atom_is_native(reduct, atom))
        {
            REDUCT_SCRATCH_PUT(reduct, args);
            return false;
        }

        result = atom->native(reduct, node->inputCount - 1, &args[1]);
    }
    else
    {
        reduct_native_fn nativeFn = reduct_builder_get_opcode_native(node->opcode);
        if (nativeFn == NULL)
        {
            REDUCT_SCRATCH_PUT(reduct, args);
            return false;
        }

        result = nativeFn(reduct, node->inputCount, args);
    }
    REDUCT_SCRATCH_PUT(reduct, args);

    reduct_rvsdg_node_t* resultNode = reduct_rvsdg_node_new_simple_constant(reduct, node->parent, result);
    reduct_rvsdg_origin_redirect_users(node->output, resultNode->output);

    return true;
}

static size_t reduct_optimize_count_origins(reduct_rvsdg_region_t* region)
{
    size_t count = region->argumentCount;
    for (reduct_rvsdg_node_t* node = region->firstNode; node != NULL; node = node->next)
    {
        count += 1;
        for (reduct_rvsdg_region_t* sub = node->firstRegion; sub != NULL; sub = sub->next)
        {
            count += reduct_optimize_count_origins(sub);
        }
    }
    return count;
}

static void reduct_optimize_clone_node_structure(reduct_t* reduct, reduct_rvsdg_node_t* oldNode,
    reduct_rvsdg_region_t* newParentRegion)
{
    reduct_rvsdg_node_t* newNode = reduct_rvsdg_node_new(reduct);
    newNode->type = oldNode->type;
    newNode->flags = oldNode->flags;

    if (newParentRegion != NULL)
    {
        reduct_rvsdg_region_add_node(newParentRegion, newNode);
    }

    if (oldNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        newNode->opcode = oldNode->opcode;
    }
    else if (oldNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        newNode->constant = oldNode->constant;
    }

    oldNode->output->map = newNode->output;

    for (uint8_t i = 0; i < oldNode->inputCount; i++)
    {
        reduct_rvsdg_node_add_input(reduct, newNode);
    }

    for (reduct_rvsdg_region_t* oldSub = oldNode->firstRegion; oldSub != NULL; oldSub = oldSub->next)
    {
        reduct_rvsdg_region_t* newSub = reduct_rvsdg_node_add_region(reduct, newNode);

        for (reduct_rvsdg_origin_t* oldArg = oldSub->firstArgument; oldArg != NULL; oldArg = oldArg->next)
        {
            reduct_rvsdg_origin_t* newArg = reduct_rvsdg_region_add_argument(reduct, newSub);
            oldArg->map = newArg;
        }

        for (reduct_rvsdg_node_t* oldSubNode = oldSub->firstNode; oldSubNode != NULL; oldSubNode = oldSubNode->next)
        {
            reduct_optimize_clone_node_structure(reduct, oldSubNode, newSub);
        }
    }
}

static void reduct_optimize_clone_node_connect_edges(reduct_t* reduct, reduct_rvsdg_node_t* oldNode)
{
    reduct_rvsdg_origin_t* newOut = oldNode->output->map;
    assert(newOut != NULL);
    reduct_rvsdg_node_t* newNode = newOut->node;

    for (uint8_t i = 0; i < oldNode->inputCount; i++)
    {
        reduct_rvsdg_user_t* oldUser = reduct_rvsdg_node_get_input(oldNode, i);
        if (oldUser == NULL || oldUser->edge == NULL || oldUser->edge->origin == NULL)
        {
            continue;
        }

        reduct_rvsdg_origin_t* oldOrigin = oldUser->edge->origin;
        reduct_rvsdg_origin_t* newOrigin = oldOrigin->map;

        reduct_rvsdg_user_t* newUser = reduct_rvsdg_node_get_input(newNode, i);
        assert(newUser != NULL);

        if (newOrigin != NULL)
        {
            reduct_rvsdg_edge_connect(reduct, newOrigin, newUser);
        }
        else
        {
            reduct_rvsdg_edge_connect(reduct, oldOrigin, newUser);
        }
    }

    reduct_rvsdg_region_t* oldSub = oldNode->firstRegion;
    reduct_rvsdg_region_t* newSub = newNode->firstRegion;
    while (oldSub != NULL)
    {
        if (oldSub->result != NULL && oldSub->result->edge != NULL && oldSub->result->edge->origin != NULL)
        {
            reduct_rvsdg_origin_t* oldRes = oldSub->result->edge->origin;
            reduct_rvsdg_origin_t* newRes = oldRes->map;
            if (newRes != NULL)
            {
                reduct_rvsdg_edge_connect(reduct, newRes, newSub->result);
            }
            else
            {
                reduct_rvsdg_edge_connect(reduct, oldRes, newSub->result);
            }
        }

        for (reduct_rvsdg_node_t* oldSubNode = oldSub->firstNode; oldSubNode != NULL; oldSubNode = oldSubNode->next)
        {
            reduct_optimize_clone_node_connect_edges(reduct, oldSubNode);
        }

        oldSub = oldSub->next;
        newSub = newSub->next;
    }
}

static void reduct_optimize_clear_map(reduct_rvsdg_region_t* region)
{
    for (reduct_rvsdg_origin_t* arg = region->firstArgument; arg != NULL; arg = arg->next)
    {
        arg->map = NULL;
    }

    for (reduct_rvsdg_node_t* node = region->firstNode; node != NULL; node = node->next)
    {
        node->output->map = NULL;
        for (reduct_rvsdg_region_t* sub = node->firstRegion; sub != NULL; sub = sub->next)
        {
            reduct_optimize_clear_map(sub);
        }
    }
}

static bool reduct_optimize_function_inlining(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || !REDUCT_OPCODE_IS_CALL(node->opcode))
    {
        return false;
    }

    reduct_rvsdg_node_t* callable = reduct_rvsdg_node_get_resolved_input_node(node, 0);
    if (callable == NULL || callable->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
    {
        return false;
    }

    reduct_rvsdg_region_t* body = callable->firstRegion;
    if (body == NULL)
    {
        return false;
    }

    uint16_t paramCount = (uint16_t)(body->argumentCount - callable->inputCount);
    bool isVariadic = callable->flags & REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC;

    if (isVariadic)
    {
        REDUCT_ERROR_ASSERT(reduct, (uint16_t)(node->inputCount - 1) >= paramCount - 1,
            "not enough arguments for variadic lambda");
    }
    else if ((uint16_t)(node->inputCount - 1) != paramCount)
    {
        return false;
    }

    reduct_rvsdg_region_t* callerRegion = node->parent;
    REDUCT_SCRATCH_GET(reduct, replacements, reduct_rvsdg_origin_t*, body->argumentCount);

    reduct_rvsdg_origin_t* arg = body->firstArgument;
    while (arg != NULL)
    {
        reduct_rvsdg_origin_t* next = arg->next;
        uint16_t capturedIndex;
        reduct_rvsdg_origin_t* replacement = NULL;

        if (reduct_rvsdg_node_argument_to_input(body, arg->index, &capturedIndex))
        {
            reduct_rvsdg_origin_t* capturedOrigin = reduct_rvsdg_node_get_input_origin(callable, capturedIndex);

            capturedOrigin = reduct_rvsdg_resolve_origin(capturedOrigin);
            replacement = reduct_optimize_localize_origin(reduct, callerRegion, capturedOrigin);
        }
        else if (isVariadic && arg->index == paramCount - 1)
        {
            reduct_rvsdg_node_t* listNode =
                reduct_rvsdg_node_new_simple_opcode(reduct, callerRegion, REDUCT_OPCODE_LIST);
            for (uint16_t i = paramCount; i < node->inputCount; i++)
            {
                reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(reduct, listNode);
                reduct_rvsdg_origin_t* argOrigin = reduct_rvsdg_node_get_input_origin(node, i);
                reduct_rvsdg_edge_connect(reduct, argOrigin, input);
            }
            replacement = listNode->output;
        }
        else
        {
            if (arg->index < paramCount)
            {
                replacement = reduct_rvsdg_node_get_input_origin(node, (uint16_t)(arg->index + 1));
            }
        }

        replacements[arg->index] = replacement;
        arg = next;
    }

    arg = body->firstArgument;
    while (arg != NULL)
    {
        arg->map = replacements[arg->index];
        arg = arg->next;
    }

    for (reduct_rvsdg_node_t* bodyNode = body->firstNode; bodyNode != NULL; bodyNode = bodyNode->next)
    {
        reduct_optimize_clone_node_structure(reduct, bodyNode, callerRegion);
    }

    for (reduct_rvsdg_node_t* bodyNode = body->firstNode; bodyNode != NULL; bodyNode = bodyNode->next)
    {
        reduct_optimize_clone_node_connect_edges(reduct, bodyNode);
    }

    if (body->result->edge != NULL && body->result->edge->origin != NULL)
    {
        reduct_rvsdg_origin_t* resultOld = body->result->edge->origin;
        reduct_rvsdg_origin_t* resultNew = resultOld->map;

        if (resultNew != NULL)
        {
            reduct_rvsdg_origin_redirect_users(node->output, resultNew);
        }
        else
        {
            reduct_rvsdg_origin_redirect_users(node->output, resultOld);
        }
    }

    reduct_optimize_clear_map(body);
    reduct_rvsdg_node_delete(reduct, node);

    REDUCT_SCRATCH_PUT(reduct, replacements);

    return true;
}

static reduct_handle_t reduct_optimize_alist_partial_from_node(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    // The result of this function will be an alist however it might store rvsdg nodes as values. So we cant use it as
    // the final output but its fine for functions which we know only acts on the keys.

    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return node->constant;
    }

    assert(node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE && node->opcode == REDUCT_OPCODE_LIST);

    reduct_list_t* list = reduct_list_new(reduct, node->inputCount);

    size_t i = 0;
    for (reduct_rvsdg_user_t* current = node->firstInput; current != NULL; current = current->next)
    {
        if (current->edge == NULL || current->edge->origin == NULL ||
            current->edge->origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
        {
            continue;
        }

        reduct_rvsdg_node_t* pairNode = current->edge->origin->node;
        if (pairNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            list->handles[i++] = pairNode->constant;
            continue;
        }

        if (pairNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || pairNode->opcode != REDUCT_OPCODE_LIST ||
            pairNode->inputCount < 2)
        {
            list->handles[i++] = REDUCT_HANDLE_FROM_RVSDG_ORIGIN(pairNode->output);
            continue;
        }

        reduct_rvsdg_node_t* keyNode = reduct_rvsdg_node_get_input_node(pairNode, 0);
        assert(keyNode != NULL && keyNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST);

        reduct_list_t* pair = reduct_list_new(reduct, pairNode->inputCount);
        pair->handles[0] = keyNode->constant;
        for (uint8_t v = 1; v < pairNode->inputCount; v++)
        {
            pair->handles[v] = REDUCT_HANDLE_FROM_RVSDG_ORIGIN(reduct_rvsdg_node_get_input_origin(pairNode, v));
        }
        list->handles[i++] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

static reduct_rvsdg_origin_t* reduct_optimize_alist_to_origin(reduct_t* reduct, reduct_handle_t handle,
    reduct_rvsdg_region_t* parent)
{
    assert(reduct != NULL);
    assert(parent != NULL);

    if (REDUCT_HANDLE_IS_RVSDG_ORIGIN(handle))
    {
        return REDUCT_HANDLE_TO_RVSDG_ORIGIN(handle);
    }

    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return reduct_rvsdg_node_new_simple_constant(reduct, parent, handle)->output;
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    reduct_rvsdg_node_t* result = reduct_rvsdg_node_new_simple_opcode(reduct, parent, REDUCT_OPCODE_LIST);
    for (uint32_t i = 0; i < list->length; i++)
    {
        reduct_handle_t pairHandle = list->handles[i];
        assert(REDUCT_HANDLE_IS_LIST(pairHandle));

        reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(pairHandle);
        assert(pair->length >= 2);

        reduct_handle_t key = pair->handles[0];

        reduct_rvsdg_node_t* pairNode = reduct_rvsdg_node_new_simple_opcode(reduct, parent, REDUCT_OPCODE_LIST);

        reduct_rvsdg_origin_t* keyOrigin = reduct_optimize_alist_to_origin(reduct, key, parent);
        assert(keyOrigin != NULL);

        reduct_rvsdg_edge_connect(reduct, keyOrigin, reduct_rvsdg_node_add_input(reduct, pairNode));

        for (uint32_t v = 1; v < pair->length; v++)
        {
            reduct_handle_t value = pair->handles[v];
            reduct_rvsdg_user_t* valueUser = reduct_rvsdg_node_add_input(reduct, pairNode);

            if (REDUCT_HANDLE_IS_RVSDG_ORIGIN(value))
            {
                reduct_rvsdg_edge_connect(reduct, REDUCT_HANDLE_TO_RVSDG_ORIGIN(value), valueUser);
            }
            else
            {
                reduct_rvsdg_origin_t* valueOrigin =
                    reduct_rvsdg_node_new_simple_constant(reduct, parent, value)->output;
                reduct_rvsdg_edge_connect(reduct, valueOrigin, valueUser);
            }
        }

        reduct_rvsdg_user_t* user = reduct_rvsdg_node_add_input(reduct, result);
        reduct_rvsdg_edge_connect(reduct, pairNode->output, user);
    }
    return result->output;
}

static bool reduct_optimize_alist_is_keys_constant(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return true;
    }

    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || node->opcode != REDUCT_OPCODE_LIST)
    {
        return false;
    }

    for (reduct_rvsdg_user_t* input = node->firstInput; input != NULL; input = input->next)
    {
        if (input->edge == NULL || input->edge->origin == NULL ||
            input->edge->origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
        {
            return false;
        }

        reduct_rvsdg_node_t* pair = input->edge->origin->node;
        if (pair->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE && pair->opcode == REDUCT_OPCODE_LIST)
        {
            reduct_rvsdg_node_t* key = reduct_rvsdg_node_get_input_node(pair, 0);
            if (key == NULL || key->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
            {
                return false;
            }
        }
        else if (pair->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            return false;
        }
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

reduct_handle_t reduct_stdlib_merge(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_get_in(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_assoc_in(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_dissoc_in(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_update_in(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_keys(reduct_t* reduct, size_t argc, reduct_handle_t* argv);
reduct_handle_t reduct_stdlib_nth(reduct_t* reduct, size_t argc, reduct_handle_t* argv);

static reduct_rvsdg_origin_t* reduct_optimize_algebraic_simplification_call(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    reduct_rvsdg_node_t* callable = reduct_rvsdg_node_get_input_node(node, 0);
    if (callable == NULL || callable->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return NULL;
    }

    if (!REDUCT_HANDLE_IS_ATOM_LIKE(callable->constant))
    {
        return NULL;
    }

    // Handles alist manipulation where only the keys are known at compile-time but not the values.
    reduct_atom_t* atom = reduct_handle_as_atom(reduct, callable->constant);
    if (atom->native == reduct_stdlib_merge)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, 1);
        if (input == NULL)
        {
            return reduct_rvsdg_node_new_simple_constant(reduct, node->parent, REDUCT_HANDLE_NIL(reduct))->output;
        }

        for (reduct_rvsdg_user_t* current = input; current != NULL; current = current->next)
        {
            if (current->edge == NULL || current->edge->origin == NULL ||
                current->edge->origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
            {
                return NULL;
            }

            reduct_rvsdg_node_t* alist = current->edge->origin->node;
            if (!reduct_optimize_alist_is_keys_constant(reduct, alist))
            {
                return NULL;
            }
        }

        uint64_t i = 0;
        REDUCT_SCRATCH_GET(reduct, args, reduct_handle_t, node->inputCount - 1);
        for (reduct_rvsdg_user_t* current = input; current != NULL; current = current->next)
        {
            args[i] = reduct_optimize_alist_partial_from_node(reduct, current->edge->origin->node);
            i++;
        }

        reduct_handle_t result = reduct_stdlib_merge(reduct, node->inputCount - 1, &args[0]);
        REDUCT_SCRATCH_PUT(reduct, args);

        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }
    if (atom->native == reduct_stdlib_get_in)
    {
        reduct_rvsdg_node_t* alistNode = reduct_rvsdg_node_get_input_node(node, 1);
        reduct_rvsdg_node_t* pathNode = reduct_rvsdg_node_get_input_node(node, 2);

        if (alistNode == NULL || pathNode == NULL || pathNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !reduct_optimize_alist_is_keys_constant(reduct, alistNode))
        {
            return NULL;
        }

        reduct_handle_t alist = reduct_optimize_alist_partial_from_node(reduct, alistNode);
        reduct_handle_t defaultVal = (node->inputCount == 4)
            ? REDUCT_HANDLE_FROM_RVSDG_NODE(reduct_rvsdg_node_get_input_node(node, 3))
            : REDUCT_HANDLE_NIL(reduct);
        reduct_handle_t result = reduct_get_in(reduct, alist, pathNode->constant, defaultVal);
        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }
    if (atom->native == reduct_stdlib_assoc_in)
    {
        reduct_rvsdg_node_t* alistNode = reduct_rvsdg_node_get_input_node(node, 1);
        reduct_rvsdg_node_t* pathNode = reduct_rvsdg_node_get_input_node(node, 2);
        reduct_rvsdg_node_t* valueNode = reduct_rvsdg_node_get_input_node(node, 3);

        if (alistNode == NULL || pathNode == NULL || valueNode == NULL ||
            pathNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !reduct_optimize_alist_is_keys_constant(reduct, alistNode))
        {
            return NULL;
        }

        reduct_handle_t alist = reduct_optimize_alist_partial_from_node(reduct, alistNode);
        reduct_handle_t value = REDUCT_HANDLE_FROM_RVSDG_NODE(valueNode);
        reduct_handle_t result = reduct_assoc_in(reduct, alist, pathNode->constant, value);
        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }
    if (atom->native == reduct_stdlib_dissoc_in)
    {
        reduct_rvsdg_node_t* alistNode = reduct_rvsdg_node_get_input_node(node, 1);
        reduct_rvsdg_node_t* pathNode = reduct_rvsdg_node_get_input_node(node, 2);

        if (alistNode == NULL || pathNode == NULL || pathNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !reduct_optimize_alist_is_keys_constant(reduct, alistNode))
        {
            return NULL;
        }

        reduct_handle_t alist = reduct_optimize_alist_partial_from_node(reduct, alistNode);
        reduct_handle_t result = reduct_dissoc_in(reduct, alist, pathNode->constant);
        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }
    if (atom->native == reduct_stdlib_update_in)
    {
        reduct_rvsdg_node_t* alistNode = reduct_rvsdg_node_get_input_node(node, 1);
        reduct_rvsdg_node_t* pathNode = reduct_rvsdg_node_get_input_node(node, 2);
        reduct_rvsdg_node_t* callableNode = reduct_rvsdg_node_get_input_node(node, 3);

        if (alistNode == NULL || pathNode == NULL || callableNode == NULL ||
            pathNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !reduct_optimize_alist_is_keys_constant(reduct, alistNode) ||
            callableNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !REDUCT_HANDLE_IS_CALLABLE(reduct, callableNode->constant))
        {
            return NULL;
        }

        reduct_handle_t alist = reduct_optimize_alist_partial_from_node(reduct, alistNode);
        reduct_handle_t result = reduct_update_in(reduct, alist, pathNode->constant, callableNode->constant);
        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }
    if (atom->native == reduct_stdlib_keys)
    {
        reduct_rvsdg_node_t* alistNode = reduct_rvsdg_node_get_input_node(node, 1);

        if (alistNode == NULL || !reduct_optimize_alist_is_keys_constant(reduct, alistNode))
        {
            return NULL;
        }

        reduct_handle_t alist = reduct_optimize_alist_partial_from_node(reduct, alistNode);
        reduct_handle_t result = reduct_keys(reduct, alist);
        return reduct_optimize_alist_to_origin(reduct, result, node->parent);
    }

    // Handles other cases where enough, but not all, information is available at compile time.
    if (atom->native == reduct_stdlib_nth)
    {
        reduct_rvsdg_node_t* handleNode = reduct_rvsdg_node_get_input_node(node, 1);
        reduct_rvsdg_node_t* indexNode = reduct_rvsdg_node_get_input_node(node, 2);
        reduct_rvsdg_node_t* defaultNode = reduct_rvsdg_node_get_input_node(node, 3);

        if (handleNode == NULL || indexNode == NULL)
        {
            return NULL;
        }

        if (handleNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || handleNode->opcode != REDUCT_OPCODE_LIST)
        {
            return NULL;
        }

        if (indexNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST ||
            !REDUCT_HANDLE_IS_NUMBER_SHAPED(indexNode->constant))
        {
            return NULL;
        }

        int64_t indexVal = (int64_t)reduct_handle_as_number(reduct, indexNode->constant);
        if (indexVal < 0)
        {
            indexVal = handleNode->inputCount + indexVal;
            if (indexVal < 0)
            {
                return NULL;
            }
        }

        if (indexVal < 0 || indexVal >= handleNode->inputCount)
        {
            return NULL;
        }

        reduct_rvsdg_node_t* elemNode = reduct_rvsdg_node_get_input_node(handleNode, indexVal);
        if (elemNode == NULL)
        {
            return NULL;
        }
        return elemNode->output;
    }

    return NULL;
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
    case REDUCT_OPCODE_CALL:
    {
        replacement = reduct_optimize_algebraic_simplification_call(reduct, node);
    }
    break;
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

static bool reduct_optimize_common_node_elimination(reduct_t* reduct, reduct_rvsdg_node_t* node)
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
            reduct_rvsdg_edge_t* edge = origin->firstEdge;
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

    reduct_rvsdg_origin_t* predicate = reduct_rvsdg_node_get_input_origin(node, 0);
    if (predicate == NULL || predicate->ownerKind != REDUCT_RVSDG_OWNER_NODE ||
        predicate->node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
    {
        return false;
    }

    reduct_rvsdg_region_t* target = node->firstRegion;
    if (REDUCT_HANDLE_IS_TRUTHY(predicate->node->constant))
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
        if (reduct_rvsdg_node_argument_to_input(target, arg->index, &inputIndex))
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

static bool reduct_optimize_dead_port_elimination(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    bool changed = false;

    for (int16_t i = node->inputCount - 1; i >= 0; i--)
    {
        bool isDead = true;
        bool mapsToAnyArg = false;

        for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
        {
            uint16_t argIndex;
            if (reduct_rvsdg_node_map_input_to_argument(node, region, i, &argIndex))
            {
                mapsToAnyArg = true;
                reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(region, argIndex);

                if (arg != NULL && arg->edgeCount > 0)
                {
                    isDead = false;
                    break;
                }
            }
        }

        if (mapsToAnyArg && isDead)
        {
            for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
            {
                uint16_t argIndex;
                if (reduct_rvsdg_node_map_input_to_argument(node, region, i, &argIndex))
                {
                    reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(region, argIndex);
                    if (arg != NULL)
                    {
                        reduct_rvsdg_region_remove_argument(arg);
                    }
                }
            }

            reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
            if (input != NULL)
            {
                reduct_rvsdg_node_remove_input(input);
            }

            changed = true;
        }
    }

    return changed;
}

static bool reduct_optimize_invariant_inputs_mapped(reduct_rvsdg_node_t* node)
{
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_origin_t* origin = reduct_rvsdg_node_get_input_origin(node, i);
        if (origin == NULL || origin->map == NULL)
        {
            return false;
        }
    }
    return true;
}

static bool reduct_optimize_invariant_code_motion_push(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->regionCount == 0 || node->parent == NULL)
    {
        return false;
    }

    reduct_rvsdg_region_t* outer = node->parent;
    bool changed = false;

    for (reduct_rvsdg_region_t* body = node->firstRegion; body != NULL; body = body->next)
    {
        for (reduct_rvsdg_origin_t* arg = body->firstArgument; arg != NULL; arg = arg->next)
        {
            uint16_t inputIndex;
            if (!reduct_rvsdg_node_argument_to_input(body, arg->index, &inputIndex))
            {
                arg->map = NULL;
                continue;
            }

            arg->map = reduct_rvsdg_node_get_input_origin(node, inputIndex);
        }

        reduct_rvsdg_node_t* curr = body->firstNode;
        while (curr != NULL)
        {
            reduct_rvsdg_node_t* next = curr->next;

            if (!reduct_optimize_node_is_pure(reduct, curr))
            {
                curr->output->map = NULL;
                curr = next;
                continue;
            }

            if (reduct_optimize_invariant_inputs_mapped(curr))
            {
                reduct_optimize_clone_node_structure(reduct, curr, outer);
                reduct_optimize_clone_node_connect_edges(reduct, curr);

                reduct_rvsdg_origin_t* cloneOutput = curr->output->map;
                assert(cloneOutput != NULL);

                reduct_rvsdg_origin_t* capture = reduct_rvsdg_region_lift_origin(reduct, body, cloneOutput);
                capture->map = cloneOutput;

                reduct_rvsdg_origin_redirect_users(curr->output, capture);
                reduct_rvsdg_node_delete(reduct, curr);

                changed = true;
            }
            else
            {
                curr->output->map = NULL;
            }

            curr = next;
        }

        reduct_optimize_clear_map(body);
    }

    return changed;
}

static bool reduct_optimize_invariant_code_motion_pull(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);

    if (node->regionCount == 0 || node->parent == NULL)
    {
        return false;
    }

    bool changed = false;
    reduct_rvsdg_user_t* inputUser = node->firstInput;
    while (inputUser != NULL)
    {
        reduct_rvsdg_user_t* nextInput = inputUser->next;

        reduct_rvsdg_origin_t* outerOrigin = (inputUser->edge != NULL) ? inputUser->edge->origin : NULL;

        if (outerOrigin == NULL || outerOrigin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
        {
            inputUser = nextInput;
            continue;
        }

        reduct_rvsdg_node_t* originNode = outerOrigin->node;
        bool isConst = (originNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST);
        bool isPureOp = reduct_optimize_node_is_pure(reduct, originNode);

        bool canSink = isConst;
        if (!canSink && isPureOp)
        {
            canSink = true;
            for (reduct_rvsdg_edge_t* edge = outerOrigin->firstEdge; edge != NULL; edge = edge->next)
            {
                if (edge->user->ownerKind != REDUCT_RVSDG_OWNER_NODE || edge->user->node != node)
                {
                    canSink = false;
                    break;
                }
            }
        }

        if (!canSink)
        {
            inputUser = nextInput;
            continue;
        }

        bool sunkAnywhere = false;
        for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
        {
            uint16_t argIndex;
            if (!reduct_rvsdg_node_map_input_to_argument(node, region, inputUser->index, &argIndex))
            {
                continue;
            }

            reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(region, argIndex);
            if (arg == NULL || arg->edgeCount == 0)
            {
                continue;
            }

            reduct_optimize_clone_node_structure(reduct, originNode, region);
            reduct_optimize_clone_node_connect_edges(reduct, originNode);

            reduct_rvsdg_origin_t* clonedOutput = originNode->output->map;

            if (!isConst)
            {
                for (uint8_t i = 0; i < originNode->inputCount; i++)
                {
                    reduct_rvsdg_user_t* oldIn = reduct_rvsdg_node_get_input(originNode, i);
                    if (oldIn != NULL && oldIn->edge != NULL)
                    {
                        reduct_rvsdg_origin_t* extOrigin = oldIn->edge->origin;
                        reduct_rvsdg_origin_t* lifted = reduct_rvsdg_region_lift_origin(reduct, region, extOrigin);
                        reduct_rvsdg_user_t* newIn = reduct_rvsdg_node_get_input(clonedOutput->node, i);

                        reduct_rvsdg_edge_disconnect(newIn->edge);
                        reduct_rvsdg_edge_connect(reduct, lifted, newIn);
                    }
                }
            }

            reduct_rvsdg_origin_redirect_users(arg, clonedOutput);
            reduct_optimize_clear_map(region);

            sunkAnywhere = true;
        }

        if (sunkAnywhere)
        {
            bool allDead = true;
            for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
            {
                uint16_t argIndex;
                if (!reduct_rvsdg_node_map_input_to_argument(node, region, inputUser->index, &argIndex))
                {
                    continue;
                }
                reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(region, argIndex);
                if (arg != NULL && arg->edgeCount > 0)
                {
                    allDead = false;
                    break;
                }
            }

            if (allDead)
            {
                if (inputUser->edge != NULL)
                {
                    reduct_rvsdg_edge_disconnect(inputUser->edge);
                }

                for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
                {
                    uint16_t argIndex;
                    if (!reduct_rvsdg_node_map_input_to_argument(node, region, inputUser->index, &argIndex))
                    {
                        continue;
                    }
                    reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(region, argIndex);
                    if (arg != NULL)
                    {
                        reduct_rvsdg_region_remove_argument(arg);
                    }
                }

                reduct_rvsdg_node_remove_input(inputUser);
                if (originNode->output->edgeCount == 0)
                {
                    reduct_rvsdg_node_delete(reduct, originNode);
                }
                changed = true;
                inputUser = nextInput;
                continue;
            }

            changed = true;
        }
        inputUser = nextInput;
    }

    return changed;
}

static bool reduct_optimize_is_parallelization_candidate(reduct_t* reduct, reduct_rvsdg_origin_t* origin)
{
    /// @todo Might need some kinda better heuristic or cost model?

    if (origin == NULL)
    {
        return false;
    }

    if (origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
    {
        return false;
    }

    reduct_rvsdg_node_t* node = origin->node;
    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }

    if (!REDUCT_OPCODE_IS_CALL(node->opcode) || REDUCT_OPCODE_IS_FORK(node->opcode))
    {
        return false;
    }

    reduct_rvsdg_edge_t* output = node->output->firstEdge;
    while (output != NULL)
    {
        if (output->user->ownerKind != REDUCT_RVSDG_OWNER_NODE)
        {
            return false;
        }

        reduct_rvsdg_node_t* input = output->user->node;
        if (input->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
        {
            return false;
        }
        output = output->next;
    }

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

    uint64_t candidates = 0;
    for (uint8_t i = 0; i < node->inputCount; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, i);
        if (input == NULL || input->edge == NULL || input->edge->origin == NULL)
        {
            continue;
        }

        reduct_rvsdg_origin_t* origin = input->edge->origin;
        if (reduct_optimize_is_parallelization_candidate(reduct, origin))
        {
            candidates++;
        }
    }

    if (candidates <= 1)
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
        if (reduct_optimize_is_parallelization_candidate(reduct, origin))
        {
            reduct_opcode_t mode = origin->node->opcode & REDUCT_OPCODE_MODE_CONST;
            origin->node->opcode = REDUCT_OPCODE_FORK | mode;
            candidates--;
        }
    }

    return true;
}

static bool reduct_optimize_commutative_swap(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || !REDUCT_OPCODE_IS_COMMUTATIVE(node->opcode) ||
        !REDUCT_OPCODE_IS_BINARY(node->opcode))
    {
        return false;
    }

    reduct_rvsdg_origin_t* left = reduct_rvsdg_node_get_input_origin(node, 0);
    reduct_rvsdg_origin_t* right = reduct_rvsdg_node_get_input_origin(node, 1);

    if (left == NULL || right == NULL)
    {
        return false;
    }

    bool leftIsConst =
        (left->ownerKind == REDUCT_RVSDG_OWNER_NODE && left->node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST);
    bool rightIsConst =
        (right->ownerKind == REDUCT_RVSDG_OWNER_NODE && right->node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST);

    if (leftIsConst && !rightIsConst)
    {
        reduct_rvsdg_user_t* leftUser = reduct_rvsdg_node_get_input(node, 0);
        reduct_rvsdg_user_t* rightUser = reduct_rvsdg_node_get_input(node, 1);

        reduct_rvsdg_edge_disconnect(leftUser->edge);
        reduct_rvsdg_edge_disconnect(rightUser->edge);

        reduct_rvsdg_edge_connect(reduct, right, leftUser);
        reduct_rvsdg_edge_connect(reduct, left, rightUser);
        return true;
    }

    return false;
}

static bool reduct_optimize_region(reduct_t* reduct, reduct_rvsdg_region_t* region,
    bool (*func)(reduct_t*, reduct_rvsdg_node_t*))
{
    assert(reduct != NULL);
    assert(region != NULL);
    assert(func != NULL);

    size_t iteration = 0;
    const size_t maxIterations = 1024;

    bool changed = true;
    while (changed && iteration++ < maxIterations)
    {
        changed = false;

        reduct_rvsdg_node_t* node = region->firstNode;
        while (node != NULL)
        {
            reduct_rvsdg_node_t* next = node->next;
            reduct_rvsdg_region_t* sub = node->firstRegion;
            while (sub != NULL)
            {
                reduct_rvsdg_region_t* next = sub->next;
                if (reduct_optimize_region(reduct, sub, func))
                {
                    changed = true;
                }
                sub = next;
            }
            if (node->output->edgeCount == 0)
            {
                reduct_rvsdg_node_delete(reduct, node);
                changed = true;
                node = next;
                continue;
            }
            if (func(reduct, node))
            {
                changed = true;
            }
            node = next;
        }
    }

    return changed;
}

static void reduct_optimize_simplify(reduct_t* reduct, reduct_rvsdg_region_t* body, reduct_optimize_flags_t flags)
{
    size_t iteration = 0;
    const size_t maxIterations = 1024;

    bool changed = true;
    while (changed && iteration++ < maxIterations)
    {
        changed = false;
        if ((flags & REDUCT_OPTIMIZE_FUNCTION_INLINING) &&
            reduct_optimize_region(reduct, body, reduct_optimize_function_inlining))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_COMMUTATIVE_SWAP) &&
            reduct_optimize_region(reduct, body, reduct_optimize_commutative_swap))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING) &&
            reduct_optimize_region(reduct, body, reduct_optimize_constant_folding))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_ALGEBRAIC_SIMPLIFICATION) &&
            reduct_optimize_region(reduct, body, reduct_optimize_algebraic_simplification))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_GAMMA_FOLDING) &&
            reduct_optimize_region(reduct, body, reduct_optimize_gamma_folding))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_COMMON_NODE_ELIMINATION) &&
            reduct_optimize_region(reduct, body, reduct_optimize_common_node_elimination))
        {
            changed = true;
        }
        if ((flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING) &&
            reduct_optimize_region(reduct, body, reduct_optimize_constant_folding))
        {
            changed = true;
        }
        if (flags & REDUCT_OPTIMIZE_DEAD_PORT_ELIMINATION &&
            reduct_optimize_region(reduct, body, reduct_optimize_dead_port_elimination))
        {
            changed = true;
        }
    }
}

static void reduct_optimize_graph(reduct_t* reduct, reduct_rvsdg_node_t* root, reduct_optimize_flags_t flags)
{
    if (root->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
    {
        return;
    }

    reduct_rvsdg_region_t* body = root->firstRegion;
    if (body == NULL)
    {
        return;
    }

    reduct_optimize_simplify(reduct, body, flags);

    if (flags & REDUCT_OPTIMIZE_INVARIANT_CODE_MOTION)
    {
        reduct_optimize_region(reduct, body, reduct_optimize_invariant_code_motion_push);
    }

    reduct_optimize_simplify(reduct, body, flags);

    if (flags & REDUCT_OPTIMIZE_INVARIANT_CODE_MOTION)
    {
        reduct_optimize_region(reduct, body, reduct_optimize_invariant_code_motion_pull);
    }

    reduct_optimize_simplify(reduct, body, flags);

    if (flags & REDUCT_OPTIMIZE_INVARIANT_CODE_MOTION)
    {
        reduct_optimize_region(reduct, body, reduct_optimize_invariant_code_motion_pull);
    }

    if (flags & REDUCT_OPTIMIZE_AUTO_PARALLELIZATION)
    {
        reduct_optimize_region(reduct, body, reduct_optimize_auto_parallelization);
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
    reduct->global->optimize.lastFlags = flags;
}
