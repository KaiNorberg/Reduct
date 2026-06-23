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
        assert(input != NULL && input->edge != NULL && input->edge->origin != NULL);
        reduct_rvsdg_origin_t* origin = input->edge->origin;
        args[i] = origin->node->constant;
    }

    reduct_handle_t result;
    if (REDUCT_OPCODE_IS_CALL(node->opcode) && node->inputCount > 0)
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, args[0]);
        if (atom->length == 0 || atom->string[atom->length - 1] == '!')
        {
            REDUCT_SCRATCH_PUT(reduct, args);
            return false;
        }

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

static void reduct_optimize_map_put(reduct_rvsdg_origin_t** keys, reduct_rvsdg_origin_t** values, size_t* count,
    reduct_rvsdg_origin_t* k, reduct_rvsdg_origin_t* v)
{
    keys[*count] = k;
    values[*count] = v;
    (*count)++;
}

static reduct_rvsdg_origin_t* reduct_optimize_map_get(reduct_rvsdg_origin_t** keys, reduct_rvsdg_origin_t** values,
    size_t count, reduct_rvsdg_origin_t* k)
{
    for (size_t i = 0; i < count; i++)
    {
        if (keys[i] == k)
        {
            return values[i];
        }
    }
    return NULL;
}

static void reduct_optimize_clone_node_structure(reduct_t* reduct, reduct_rvsdg_node_t* oldNode,
    reduct_rvsdg_region_t* newParentRegion, reduct_rvsdg_origin_t** keys, reduct_rvsdg_origin_t** values,
    size_t* mapCount)
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

    reduct_optimize_map_put(keys, values, mapCount, oldNode->output, newNode->output);

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
            reduct_optimize_map_put(keys, values, mapCount, oldArg, newArg);
        }

        for (reduct_rvsdg_node_t* oldSubNode = oldSub->firstNode; oldSubNode != NULL; oldSubNode = oldSubNode->next)
        {
            reduct_optimize_clone_node_structure(reduct, oldSubNode, newSub, keys, values, mapCount);
        }
    }
}

static void reduct_optimize_clone_node_connect_edges(reduct_t* reduct, reduct_rvsdg_node_t* oldNode,
    reduct_rvsdg_origin_t** keys, reduct_rvsdg_origin_t** values, size_t mapCount)
{
    reduct_rvsdg_origin_t* newOut = reduct_optimize_map_get(keys, values, mapCount, oldNode->output);
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
        reduct_rvsdg_origin_t* newOrigin = reduct_optimize_map_get(keys, values, mapCount, oldOrigin);

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
            reduct_rvsdg_origin_t* newRes = reduct_optimize_map_get(keys, values, mapCount, oldRes);
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
            reduct_optimize_clone_node_connect_edges(reduct, oldSubNode, keys, values, mapCount);
        }

        oldSub = oldSub->next;
        newSub = newSub->next;
    }
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

    REDUCT_SCRATCH_GET(reduct, replacements, reduct_rvsdg_origin_t*, body->argumentCount);

    reduct_rvsdg_origin_t* arg = body->firstArgument;
    while (arg != NULL)
    {
        reduct_rvsdg_origin_t* next = arg->next;

        uint16_t capturedIndex;
        reduct_rvsdg_origin_t* replacement = NULL;

        if (reduct_rvsdg_node_map_argument_to_input(callable, body, arg->index, &capturedIndex))
        {
            replacement = reduct_rvsdg_node_get_input_origin(callable, capturedIndex);
        }
        else if (isVariadic && arg->index == paramCount - 1)
        {
            reduct_rvsdg_node_t* listNode =
                reduct_rvsdg_node_new_simple_opcode(reduct, node->parent, REDUCT_OPCODE_LIST);
            for (uint16_t i = paramCount; i < node->inputCount; i++)
            {
                reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(reduct, listNode);
                reduct_rvsdg_origin_t* argOrigin = reduct_rvsdg_node_get_input_origin(node, i);
                assert(argOrigin != NULL);
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

    size_t totalOrigins = reduct_optimize_count_origins(body);
    REDUCT_SCRATCH_GET(reduct, mapKeys, reduct_rvsdg_origin_t*, totalOrigins);
    REDUCT_SCRATCH_GET(reduct, mapValues, reduct_rvsdg_origin_t*, totalOrigins);
    size_t mapCount = 0;

    arg = body->firstArgument;
    while (arg != NULL)
    {
        reduct_optimize_map_put(mapKeys, mapValues, &mapCount, arg, replacements[arg->index]);
        arg = arg->next;
    }

    reduct_rvsdg_region_t* callerRegion = node->parent;
    for (reduct_rvsdg_node_t* bodyNode = body->firstNode; bodyNode != NULL; bodyNode = bodyNode->next)
    {
        reduct_optimize_clone_node_structure(reduct, bodyNode, callerRegion, mapKeys, mapValues, &mapCount);
    }

    for (reduct_rvsdg_node_t* bodyNode = body->firstNode; bodyNode != NULL; bodyNode = bodyNode->next)
    {
        reduct_optimize_clone_node_connect_edges(reduct, bodyNode, mapKeys, mapValues, mapCount);
    }

    if (body->result != NULL && body->result->edge != NULL && body->result->edge->origin != NULL)
    {
        reduct_rvsdg_origin_t* resultOld = body->result->edge->origin;
        reduct_rvsdg_origin_t* resultNew = reduct_optimize_map_get(mapKeys, mapValues, mapCount, resultOld);

        if (resultNew != NULL)
        {
            reduct_rvsdg_origin_redirect_users(node->output, resultNew);
        }
        else
        {
            reduct_rvsdg_origin_redirect_users(node->output, resultOld);
        }
    }

    reduct_rvsdg_node_delete(reduct, node);

    REDUCT_SCRATCH_PUT(reduct, mapValues);
    REDUCT_SCRATCH_PUT(reduct, mapKeys);
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
        reduct_rvsdg_node_t* pairNode = current->edge->origin->node;
        if (pairNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            list->handles[i++] = pairNode->constant;
            continue;
        }

        if (pairNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE || pairNode->opcode != REDUCT_OPCODE_LIST ||
            pairNode->inputCount < 2)
        {
            list->handles[i++] = REDUCT_HANDLE_FROM_RVSDG_NODE(pairNode);
            continue;
        }

        reduct_rvsdg_node_t* keyNode = reduct_rvsdg_node_get_input_node(pairNode, 0);
        if (keyNode == NULL || keyNode->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            list->handles[i++] = REDUCT_HANDLE_FROM_RVSDG_NODE(pairNode);
            continue;
        }

        bool allValuesPresent = true;
        for (uint8_t v = 1; v < pairNode->inputCount; v++)
        {
            if (reduct_rvsdg_node_get_input_origin(pairNode, v) == NULL)
            {
                allValuesPresent = false;
                break;
            }
        }

        if (!allValuesPresent)
        {
            list->handles[i++] = REDUCT_HANDLE_FROM_RVSDG_NODE(pairNode);
            continue;
        }

        reduct_list_t* pair = reduct_list_new(reduct, pairNode->inputCount);
        pair->handles[0] = keyNode->constant;
        for (uint8_t v = 1; v < pairNode->inputCount; v++)
        {
            reduct_rvsdg_origin_t* valueOrigin = reduct_rvsdg_node_get_input_origin(pairNode, v);
            pair->handles[v] = REDUCT_HANDLE_FROM_RVSDG_NODE(valueOrigin->node);
        }
        list->handles[i++] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

static reduct_rvsdg_node_t* reduct_optimize_alist_to_node(reduct_t* reduct, reduct_handle_t handle,
    reduct_rvsdg_region_t* parent)
{
    assert(reduct != NULL);
    assert(parent != NULL);

    if (REDUCT_HANDLE_IS_RVSDG_NODE(handle))
    {
        return REDUCT_HANDLE_TO_RVSDG_NODE(handle);
    }

    assert(parent != NULL);

    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return reduct_rvsdg_node_new_simple_constant(reduct, parent, handle);
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    reduct_rvsdg_node_t* result = reduct_rvsdg_node_new_simple_opcode(reduct, parent, REDUCT_OPCODE_LIST);
    for (uint32_t i = 0; i < list->length; i++)
    {
        reduct_handle_t pairHandle = list->handles[i];
        reduct_rvsdg_node_t* pairNode;

        if (!REDUCT_HANDLE_IS_LIST(pairHandle) || REDUCT_HANDLE_TO_LIST(pairHandle)->length < 2)
        {
            pairNode = reduct_optimize_alist_to_node(reduct, pairHandle, parent);
        }
        else
        {
            reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(pairHandle);
            reduct_handle_t key = pair->handles[0];

            pairNode = reduct_rvsdg_node_new_simple_opcode(reduct, parent, REDUCT_OPCODE_LIST);
            reduct_rvsdg_user_t* keyUser = reduct_rvsdg_node_add_input(reduct, pairNode);

            reduct_rvsdg_node_t* keyNode = reduct_optimize_alist_to_node(reduct, key, parent);
            if (keyNode != NULL)
            {
                reduct_rvsdg_edge_connect(reduct, keyNode->output, keyUser);
            }

            for (uint32_t v = 1; v < pair->length; v++)
            {
                reduct_handle_t value = pair->handles[v];
                reduct_rvsdg_user_t* valueUser = reduct_rvsdg_node_add_input(reduct, pairNode);

                if (REDUCT_HANDLE_IS_RVSDG_NODE(value))
                {
                    reduct_rvsdg_edge_connect(reduct, REDUCT_HANDLE_TO_RVSDG_NODE(value)->output, valueUser);
                }
                else
                {
                    reduct_rvsdg_edge_connect(reduct, reduct_optimize_alist_to_node(reduct, value, parent)->output,
                        valueUser);
                }
            }
        }

        reduct_rvsdg_user_t* user = reduct_rvsdg_node_add_input(reduct, result);
        reduct_rvsdg_edge_connect(reduct, pairNode->output, user);
    }
    return result;
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

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        if (input->edge == NULL || input->edge->origin == NULL)
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

        input = input->next;
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

        reduct_rvsdg_node_t* resultNode = reduct_optimize_alist_to_node(reduct, result, node->parent);
        return resultNode->output;
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
        return reduct_optimize_alist_to_node(reduct, result, node->parent)->output;
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
        return reduct_optimize_alist_to_node(reduct, result, node->parent)->output;
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
        return reduct_optimize_alist_to_node(reduct, result, node->parent)->output;
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
        return reduct_optimize_alist_to_node(reduct, result, node->parent)->output;
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
        return reduct_optimize_alist_to_node(reduct, result, node->parent)->output;
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

        return reduct_rvsdg_node_get_input_node(handleNode, indexVal)->output;
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

    reduct_rvsdg_edge_t* output = node->output->edges;
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

static void reduct_optimize_parallelization_region(reduct_t* reduct, reduct_rvsdg_region_t* region)
{
    reduct_rvsdg_node_t* curr = region->firstNode;
    while (curr != NULL)
    {
        reduct_rvsdg_region_t* sub = curr->firstRegion;
        while (sub != NULL)
        {
            reduct_optimize_parallelization_region(reduct, sub);
            sub = sub->next;
        }

        reduct_optimize_auto_parallelization(reduct, curr);
        curr = curr->next;
    }
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

    if (flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING && reduct_optimize_constant_folding(reduct, node))
    {
        changed = true;
    }

    if (flags & REDUCT_OPTIMIZE_FUNCTION_INLINING && reduct_optimize_function_inlining(reduct, node))
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

    return changed;
}

static bool reduct_optimize_region(reduct_t* reduct, reduct_rvsdg_region_t* region, reduct_optimize_flags_t flags)
{
    bool changed = false;
    reduct_rvsdg_node_t* node = region->firstNode;
    while (node != NULL)
    {
        reduct_rvsdg_node_t* next = node->next;
        reduct_rvsdg_region_t* sub = node->firstRegion;
        while (sub != NULL)
        {
            reduct_rvsdg_region_t* next = sub->next;
            if (reduct_optimize_region(reduct, sub, flags))
            {
                changed = true;
            }
            sub = next;
        }
        if (reduct_optimize_node(reduct, node, flags))
        {
            changed = true;
        }
        node = next;
    }

    return changed;
}

static void reduct_optimize_graph(reduct_t* reduct, reduct_rvsdg_node_t* root, reduct_optimize_flags_t flags)
{
    bool changed = true;
    uint32_t iterations = 0;
    const uint32_t MAX_ITERATIONS = 1024;
    reduct_optimize_flags_t iterativeFlags = flags;

    while (changed && iterations < MAX_ITERATIONS)
    {
        changed = false;

        if (root->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA)
        {
            changed |= reduct_optimize_region(reduct, root->firstRegion, flags);
        }

        iterations++;
    }

    if (flags & REDUCT_OPTIMIZE_AUTO_PARALLELIZATION)
    {
        if (root->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA)
        {
            reduct_optimize_parallelization_region(reduct, root->firstRegion);
        }
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
