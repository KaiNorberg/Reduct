#include "reduct/error.h"
#include "reduct/inst.h"
#include <reduct/build.h>
#include <reduct/core.h>
#include <reduct/handle.h>
#include <reduct/rvsdg.h>

static inline reduct_builder_local_t* reduct_builder_add_local_to_scope(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    struct reduct_atom* key, reduct_rvsdg_origin_t* value)
{
    assert(builder != NULL);
    assert(key != NULL);
    assert(scope != NULL);

    if (scope->localAmount == REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE_LAST(builder, "too many local variables in scope, limit is %d", REDUCT_REGISTER_MAX);
    }

    reduct_builder_local_t* local = &scope->locals[scope->localAmount++];
    local->key = key;
    local->value = value;
    return local;
}

static inline reduct_builder_local_t* reduct_builder_add_local(reduct_builder_t* builder, struct reduct_atom* key,
    reduct_rvsdg_origin_t* value)
{
    return reduct_builder_add_local_to_scope(builder, builder->scope, key, value);
}

static reduct_rvsdg_origin_t* reduct_builder_lift_origin(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    reduct_rvsdg_origin_t* outerValue)
{
    assert(builder != NULL);
    assert(scope != NULL);
    assert(outerValue != NULL);

    if (scope->region != NULL && scope->region->parent != NULL)
    {
        reduct_rvsdg_node_t* node = scope->region->parent;

        if (node->type == REDUCT_RVSDG_NODE_TYPE_PHI && outerValue->ownerKind == REDUCT_RVSDG_OWNER_NODE &&
            outerValue->node == node)
        {
            return reduct_rvsdg_region_get_argument(scope->region, outerValue->index);
        }

        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);
        reduct_rvsdg_user_t* input = node->firstInput;
        while (input != NULL)
        {
            if (input->use != NULL && input->use->origin == outerValue)
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
            input = reduct_rvsdg_node_add_input(builder->reduct, node);
            reduct_rvsdg_edge_connect(builder->reduct, outerValue, input);

            resultArg = NULL;
            reduct_rvsdg_region_t* region = node->firstRegion;
            while (region != NULL)
            {
                reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_add_argument(builder->reduct, region);
                if (region == scope->region)
                {
                    resultArg = arg;
                }
                region = region->next;
            }
        }
        else
        {
            uint16_t argIndex;
            if (node->type == REDUCT_RVSDG_NODE_TYPE_GAMMA)
            {
                argIndex = input->index - info->dataInputOffset;
            }
            else
            {
                argIndex = (scope->region->argumentCount - node->inputCount) + input->index;
            }
            resultArg = reduct_rvsdg_region_get_argument(scope->region, argIndex);
        }

        return resultArg;
    }

    return outerValue;
}

static reduct_rvsdg_origin_t* reduct_builder_resolve_origin(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    reduct_rvsdg_origin_t* origin)
{
    assert(builder != NULL);
    assert(scope != NULL);
    assert(origin != NULL);

    if (origin->ownerKind == REDUCT_RVSDG_OWNER_REGION && origin->region == scope->region)
    {
        return origin;
    }

    if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE && origin->node->parent == scope->region)
    {
        return origin;
    }

    if (scope->parent == NULL)
    {
        return origin;
    }

    reduct_rvsdg_origin_t* outerValue = reduct_builder_resolve_origin(builder, scope->parent, origin);

    reduct_rvsdg_region_t* chain[16];
    int depth = 0;
    reduct_rvsdg_region_t* curr = scope->region;
    reduct_rvsdg_region_t* target = scope->parent->region;

    while (curr != NULL && curr != target)
    {
        assert(depth < 16);
        chain[depth++] = curr;
        if (curr->parent == NULL)
        {
            break;
        }
        curr = curr->parent->parent;
    }

    reduct_rvsdg_origin_t* val = outerValue;
    for (int i = depth - 1; i >= 0; i--)
    {
        reduct_builder_scope_t dummy = {.region = chain[i], .parent = NULL};
        val = reduct_builder_lift_origin(builder, &dummy, val);
    }

    return val;
}

static reduct_builder_local_t* reduct_builder_resolve_local(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    struct reduct_atom* key)
{
    assert(builder != NULL);
    assert(scope != NULL);
    assert(key != NULL);

    for (size_t i = 0; i < scope->localAmount; i++)
    {
        if (scope->locals[i].key == key)
        {
            return &scope->locals[i];
        }
    }

    if (scope->parent == NULL)
    {
        return NULL;
    }

    reduct_builder_local_t* outer = reduct_builder_resolve_local(builder, scope->parent, key);
    if (outer == NULL)
    {
        return NULL;
    }

    if (outer->value == NULL)
    {
        reduct_builder_scope_t* scope = builder->scope;
        while (scope != NULL && scope->lambdaNode == NULL)
        {
            scope = scope->parent;
        }

        if (scope != NULL)
        {
            reduct_rvsdg_node_t* lambda = scope->lambdaNode;

            if (!reduct_rvsdg_node_is_inside_phi(lambda))
            {
                reduct_rvsdg_node_phi_wrap_lambda(builder->reduct, lambda);
            }

            reduct_rvsdg_node_t* phi = lambda->parent->parent;
            
            reduct_rvsdg_origin_t* callable = reduct_builder_resolve_origin(builder, scope, phi->firstOutput);
            return reduct_builder_add_local_to_scope(builder, scope, key, callable);
        }

        REDUCT_ERROR_COMPILE_LAST(builder, "variable '%.*s' is not yet defined", key->length, key->string);
    }

    reduct_rvsdg_origin_t* resultArg = reduct_builder_lift_origin(builder, scope, outer->value);
    return reduct_builder_add_local_to_scope(builder, scope, key, resultArg);
}

static reduct_rvsdg_origin_t* reduct_build_quote(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "quote: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
        reduct_list_second(builder->reduct, list))
        ->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_recur(reduct_builder_t* builder, reduct_list_t* list)
{
    reduct_builder_scope_t* s = builder->scope;
    while (s != NULL && s->lambdaNode == NULL)
    {
        s = s->parent;
    }

    REDUCT_ERROR_COMPILE_ASSERT(builder, s != NULL, "recur: must be called within a lambda");

    reduct_rvsdg_node_t* lambda = s->lambdaNode;

    if (!reduct_rvsdg_node_is_inside_phi(lambda))
    {
        reduct_rvsdg_node_phi_wrap_lambda(builder->reduct, lambda);
    }

    reduct_rvsdg_node_t* phi = lambda->parent->parent;
    reduct_rvsdg_origin_t* callable = reduct_builder_resolve_origin(builder, builder->scope, phi->firstOutput);

    reduct_rvsdg_node_t* call = reduct_rvsdg_node_new_call(builder->reduct, builder->scope->region, callable);

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 1);
    reduct_handle_t arg;
    while (reduct_list_iter_next(&iter, &arg))
    {
        reduct_rvsdg_origin_t* argOrigin = reduct_build_handle(builder, arg);
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(builder->reduct, call);
        reduct_rvsdg_edge_connect(builder->reduct, argOrigin, input);
    }

    return call->firstOutput;
}

static reduct_handle_t reduct_build_list_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    reduct_list_t* list = reduct_list_new(reduct);
    for (size_t i = 0; i < argc; i++)
    {
        reduct_list_push(reduct, list, argv[i]);
    }
    return REDUCT_HANDLE_FROM_LIST(list);
}

static reduct_rvsdg_origin_t* reduct_build_list(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_generic_list(builder, REDUCT_HANDLE_FROM_LIST(list), 1);
}

static reduct_rvsdg_origin_t* reduct_build_do(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_generic_block(builder, REDUCT_HANDLE_FROM_LIST(list), 1);
}

static reduct_rvsdg_origin_t* reduct_build_lambda(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "lambda: expected 2 arguments, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t args = reduct_list_second(builder->reduct, list);
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_LIST(args), "lambda: parameter list must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(args));

    reduct_rvsdg_node_t* lambda = reduct_rvsdg_node_new_lambda(builder->reduct, builder->scope->region);

    reduct_builder_scope_t scope = {.region = lambda->firstRegion, .parent = builder->scope, .lambdaNode = lambda};
    builder->scope = &scope;

    reduct_list_iter_t iter = REDUCT_LIST_ITER(REDUCT_HANDLE_TO_LIST(args));
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_handle_t param = chunk.handles[i];
            REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_ATOM(param),
                "lambda: parameter must be an atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(param));

            reduct_rvsdg_origin_t* value = reduct_rvsdg_region_add_argument(builder->reduct, lambda->firstRegion);
            reduct_builder_add_local(builder, REDUCT_HANDLE_TO_ATOM(param), value);
        }
    }

    reduct_rvsdg_origin_t* origin = reduct_build_generic_block(builder, REDUCT_HANDLE_FROM_LIST(list), 2);
    reduct_rvsdg_edge_connect(builder->reduct, origin, lambda->firstRegion->firstResult);

    builder->scope = builder->scope->parent;

    if (reduct_rvsdg_node_is_inside_phi(lambda))
    {
        return lambda->parent->parent->firstOutput;
    }

    return lambda->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_thread(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 2, "->: expected at least 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t current = reduct_list_nth(builder->reduct, list, 1);

    for (size_t i = 2; i < list->length; i++)
    {
        reduct_handle_t step = reduct_list_nth(builder->reduct, list, i);
        reduct_list_t* next = reduct_list_new(builder->reduct);

        reduct_item_t* nextItem = REDUCT_CONTAINER_OF(next, reduct_item_t, list);
        if (REDUCT_HANDLE_IS_ITEM(step))
        {
            reduct_item_t* stepItem = REDUCT_HANDLE_TO_ITEM(step);
            nextItem->inputId = stepItem->inputId;
            nextItem->position = stepItem->position;
        }

        if (REDUCT_HANDLE_IS_ATOM(step))
        {
            reduct_list_push(builder->reduct, next, step);
            reduct_list_push(builder->reduct, next, current);
        }
        else if (REDUCT_HANDLE_IS_LIST(step))
        {
            reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(step);
            REDUCT_ERROR_COMPILE_ASSERT(builder, stepList->length > 0, "->: step cannot be an empty list");

            reduct_list_push(builder->reduct, next, reduct_list_first(builder->reduct, stepList));
            reduct_list_push(builder->reduct, next, current);

            reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(stepList, 1);
            reduct_handle_t arg;
            while (reduct_list_iter_next(&iter, &arg))
            {
                reduct_list_push(builder->reduct, next, arg);
            }
        }
        else
        {
            REDUCT_ERROR_COMPILE(builder, step, "->: expected atom or list step, got %s",
                REDUCT_HANDLE_GET_TYPE_STRING(step));
        }

        current = REDUCT_HANDLE_FROM_LIST(next);
    }

    return reduct_build_handle(builder, current);
}

static reduct_rvsdg_origin_t* reduct_build_def(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 3, "def: expected 2 arguments, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t name = reduct_list_second(builder->reduct, list);
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_ATOM(name), "def: first argument must be a symbol, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(name));

    reduct_builder_local_t* local = reduct_builder_add_local(builder, REDUCT_HANDLE_TO_ATOM(name), NULL);
    local->value = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, 2));;
    return local->value;
}

static reduct_rvsdg_origin_t* reduct_build_if(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 3 || list->length == 4,
        "if: expected 2 or 3 arguments, got %zu", (size_t)list->length - 1);

    reduct_handle_t cond = reduct_list_nth(builder->reduct, list, 1);
    reduct_handle_t then = reduct_list_nth(builder->reduct, list, 2);
    reduct_handle_t els =
        (list->length == 4) ? reduct_list_nth(builder->reduct, list, 3) : REDUCT_HANDLE_NIL(builder->reduct);

    reduct_rvsdg_origin_t* condOrigin = reduct_build_handle(builder, cond);
    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);

    reduct_rvsdg_edge_connect(builder->reduct, condOrigin, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_t* thenRegion = gamma->firstRegion;
    reduct_rvsdg_region_t* elseRegion = gamma->firstRegion->next;

    {
        reduct_rvsdg_region_add_result(builder->reduct, thenRegion);
        reduct_builder_scope_t scope = {.region = thenRegion, .parent = builder->scope};
        builder->scope = &scope;

        reduct_rvsdg_origin_t* result = reduct_build_handle(builder, then);
        reduct_rvsdg_edge_connect(builder->reduct, result, thenRegion->firstResult);
        builder->scope = builder->scope->parent;
    }

    {
        reduct_rvsdg_region_add_result(builder->reduct, elseRegion);
        reduct_builder_scope_t scope = {.region = elseRegion, .parent = builder->scope};
        builder->scope = &scope;

        reduct_rvsdg_origin_t* result = reduct_build_handle(builder, els);
        reduct_rvsdg_edge_connect(builder->reduct, result, elseRegion->firstResult);
        builder->scope = builder->scope->parent;
    }

    return gamma->firstOutput;
}

static reduct_list_t* reduct_build_get_pair(reduct_builder_t* builder, reduct_handle_t handle, const char* name)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_LIST(handle), "%s: clause must be a list", name);
    reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(handle);
    REDUCT_ERROR_COMPILE_ASSERT(builder, pair->length == 2, "%s: clause must have 2 items", name);
    return pair;
}

static reduct_rvsdg_origin_t* reduct_build_and_or_internal(reduct_builder_t* builder, reduct_list_t* list, size_t index,
    bool isOr)
{
    reduct_rvsdg_origin_t* current = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, index));
    if (index == list->length - 1)
    {
        return current;
    }

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);
    reduct_rvsdg_edge_connect(builder->reduct, current, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_t* truthyRegion = gamma->firstRegion;
    reduct_rvsdg_region_t* falsyRegion = gamma->firstRegion->next;
    reduct_rvsdg_region_add_result(builder->reduct, truthyRegion);
    reduct_rvsdg_region_add_result(builder->reduct, falsyRegion);

    if (!isOr)
    {
        {
            reduct_builder_scope_t scope = {.region = truthyRegion, .parent = builder->scope};
            reduct_builder_scope_t* prev = builder->scope;
            builder->scope = &scope;
            reduct_rvsdg_origin_t* res = reduct_build_and_or_internal(builder, list, index + 1, false);
            reduct_rvsdg_edge_connect(builder->reduct, res, truthyRegion->firstResult);
            builder->scope = prev;
        }
        {
            reduct_builder_scope_t scope = {.region = falsyRegion, .parent = builder->scope};
            reduct_rvsdg_edge_connect(builder->reduct, reduct_builder_resolve_origin(builder, &scope, current),
                falsyRegion->firstResult);
        }
    }
    else
    {
        {
            reduct_builder_scope_t scope = {.region = truthyRegion, .parent = builder->scope};
            reduct_rvsdg_edge_connect(builder->reduct, reduct_builder_resolve_origin(builder, &scope, current),
                truthyRegion->firstResult);
        }
        {
            reduct_builder_scope_t scope = {.region = falsyRegion, .parent = builder->scope};
            reduct_builder_scope_t* prev = builder->scope;
            builder->scope = &scope;
            reduct_rvsdg_origin_t* res = reduct_build_and_or_internal(builder, list, index + 1, true);
            reduct_rvsdg_edge_connect(builder->reduct, res, falsyRegion->firstResult);
            builder->scope = prev;
        }
    }

    return gamma->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_not(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "not: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, reduct_list_second(builder->reduct, list));

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);
    reduct_rvsdg_edge_connect(builder->reduct, arg, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion);       // Truthy
    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion->next); // Falsy

    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion, REDUCT_HANDLE_FALSE())->firstOutput,
        gamma->firstRegion->firstResult);
    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion->next, REDUCT_HANDLE_TRUE())
            ->firstOutput,
        gamma->firstRegion->next->firstResult);

    return gamma->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_and(reduct_builder_t* builder, reduct_list_t* list)
{
    if (list->length == 1)
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FALSE())
            ->firstOutput;
    return reduct_build_and_or_internal(builder, list, 1, false);
}

static reduct_rvsdg_origin_t* reduct_build_or(reduct_builder_t* builder, reduct_list_t* list)
{
    if (list->length == 1)
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FALSE())
            ->firstOutput;
    return reduct_build_and_or_internal(builder, list, 1, true);
}

static reduct_rvsdg_origin_t* reduct_build_variadic_op(reduct_builder_t* builder, reduct_list_t* list,
    reduct_opcode_t op)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 2, "operator: expected at least 1 argument");
    reduct_rvsdg_origin_t* acc = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, 1));

    if (list->length == 2)
    {
        if (op == REDUCT_OPCODE_SUB)
        {
            reduct_rvsdg_origin_t* zero = reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
                REDUCT_HANDLE_FROM_NUMBER(0.0))
                                              ->firstOutput;
            return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_SUB, zero,
                acc)
                ->firstOutput;
        }
        if (op == REDUCT_OPCODE_DIV)
        {
            reduct_rvsdg_origin_t* one = reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
                REDUCT_HANDLE_FROM_NUMBER(1.0))
                                             ->firstOutput;
            return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_DIV, one,
                acc)
                ->firstOutput;
        }
        return acc;
    }

    for (size_t i = 2; i < list->length; i++)
    {
        reduct_rvsdg_origin_t* next = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, i));
        acc = reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, op, acc, next)->firstOutput;
    }
    return acc;
}

static reduct_rvsdg_origin_t* reduct_build_inc(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "++: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, reduct_list_second(builder->reduct, list));
    reduct_rvsdg_origin_t* one =
        reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FROM_NUMBER(1.0))
            ->firstOutput;
    return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_ADD, arg, one)
        ->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_dec(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "--: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, reduct_list_second(builder->reduct, list));
    reduct_rvsdg_origin_t* one =
        reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FROM_NUMBER(1.0))
            ->firstOutput;
    return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_SUB, arg, one)
        ->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_comparison_internal(reduct_builder_t* builder, reduct_list_t* list,
    size_t index, reduct_opcode_t op, reduct_rvsdg_origin_t* left)
{
    reduct_rvsdg_origin_t* right = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, index));
    reduct_rvsdg_origin_t* cmp =
        reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, op, left, right)->firstOutput;

    if (index == list->length - 1)
    {
        return cmp;
    }

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);
    reduct_rvsdg_edge_connect(builder->reduct, cmp, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion);       // Truthy
    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion->next); // Falsy

    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion->next, REDUCT_HANDLE_FALSE())
            ->firstOutput,
        gamma->firstRegion->next->firstResult);

    {
        reduct_builder_scope_t scope = {.region = gamma->firstRegion, .parent = builder->scope};
        reduct_builder_scope_t* prev = builder->scope;
        builder->scope = &scope;
        reduct_rvsdg_origin_t* next = reduct_build_comparison_internal(builder, list, index + 1, op,
            reduct_builder_resolve_origin(builder, &scope, right));
        reduct_rvsdg_edge_connect(builder->reduct, next, gamma->firstRegion->firstResult);
        builder->scope = prev;
    }

    return gamma->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_chained_comparison(reduct_builder_t* builder, reduct_list_t* list,
    reduct_opcode_t op)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "comparison: expected at least 2 arguments");
    reduct_rvsdg_origin_t* left = reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, 1));
    return reduct_build_comparison_internal(builder, list, 2, op, left);
}

static reduct_rvsdg_origin_t* reduct_build_cond_internal(reduct_builder_t* builder, reduct_list_t* list, size_t index)
{
    if (index == list->length)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
            ->firstOutput;
    }

    reduct_list_t* pair = reduct_build_get_pair(builder, reduct_list_nth(builder->reduct, list, index), "cond");
    reduct_rvsdg_origin_t* cond = reduct_build_handle(builder, reduct_list_first(builder->reduct, pair));

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);
    reduct_rvsdg_edge_connect(builder->reduct, cond, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion);       // Truthy
    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion->next); // Falsy

    {
        reduct_builder_scope_t scope = {.region = gamma->firstRegion, .parent = builder->scope};
        reduct_builder_scope_t* prev = builder->scope;
        builder->scope = &scope;
        reduct_rvsdg_origin_t* res = reduct_build_handle(builder, reduct_list_second(builder->reduct, pair));
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->firstResult);
        builder->scope = prev;
    }

    {
        reduct_builder_scope_t scope = {.region = gamma->firstRegion->next, .parent = builder->scope};
        reduct_builder_scope_t* prev = builder->scope;
        builder->scope = &scope;
        reduct_rvsdg_origin_t* res = reduct_build_cond_internal(builder, list, index + 1);
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->next->firstResult);
        builder->scope = prev;
    }

    return gamma->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_cond(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_cond_internal(builder, list, 1);
}

static reduct_rvsdg_origin_t* reduct_build_match_internal(reduct_builder_t* builder, reduct_list_t* list, size_t index,
    reduct_rvsdg_origin_t* target)
{
    if (index == list->length - 1)
    {
        return reduct_build_handle(builder, reduct_list_nth(builder->reduct, list, index));
    }

    reduct_list_t* pair = reduct_build_get_pair(builder, reduct_list_nth(builder->reduct, list, index), "match");
    reduct_rvsdg_origin_t* caseVal = reduct_build_handle(builder, reduct_list_first(builder->reduct, pair));

    reduct_rvsdg_origin_t* cmp =
        reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_EQ, target, caseVal)
            ->firstOutput;

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region, 2);
    reduct_rvsdg_edge_connect(builder->reduct, cmp, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion);       // Truthy
    reduct_rvsdg_region_add_result(builder->reduct, gamma->firstRegion->next); // Falsy

    {
        reduct_builder_scope_t scope = {.region = gamma->firstRegion, .parent = builder->scope};
        reduct_builder_scope_t* prev = builder->scope;
        builder->scope = &scope;
        reduct_rvsdg_origin_t* res = reduct_build_handle(builder, reduct_list_second(builder->reduct, pair));
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->firstResult);
        builder->scope = prev;
    }

    {
        reduct_builder_scope_t scope = {.region = gamma->firstRegion->next, .parent = builder->scope};
        reduct_builder_scope_t* prev = builder->scope;
        builder->scope = &scope;
        reduct_rvsdg_origin_t* res = reduct_build_match_internal(builder, list, index + 1,
            reduct_builder_resolve_origin(builder, &scope, target));
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->next->firstResult);
        builder->scope = prev;
    }

    return gamma->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_match(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "match: expected at least target and one case");
    reduct_rvsdg_origin_t* target = reduct_build_handle(builder, reduct_list_second(builder->reduct, list));
    return reduct_build_match_internal(builder, list, 2, target);
}

#define REDUCT_BUILD_NATIVE_ARITH(_name, _op, _identity) \
    static reduct_handle_t reduct_build_##_name##_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_FROM_NUMBER((double)_identity); \
        } \
        if (argc == 1) \
        { \
            reduct_handle_t res; \
            reduct_handle_t id = REDUCT_HANDLE_FROM_NUMBER((double)_identity); \
            REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, id, argv[0], _op); \
            return res; \
        } \
        reduct_handle_t res = argv[0]; \
        for (size_t i = 1; i < argc; i++) \
        { \
            REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, res, argv[i], _op); \
        } \
        return res; \
    }

#define REDUCT_BUILD_NATIVE_LOGIC(_name, _shortCircuitTruth) \
    static reduct_handle_t reduct_build_##_name##_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_UNUSED(reduct); \
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_FALSE(); \
        } \
        reduct_handle_t res = argv[0]; \
        for (size_t i = 0; i < argc; i++) \
        { \
            res = argv[i]; \
            if (REDUCT_HANDLE_IS_TRUTHY(res) == (_shortCircuitTruth)) \
            { \
                return res; \
            } \
        } \
        return res; \
    }

#define REDUCT_BUILD_NATIVE_BITWISE(_name, _op) \
    static reduct_handle_t reduct_build_##_name##_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 2, #_op ": expected at least 2 argument(s), got %zu", (size_t)argc); \
        int64_t res = reduct_handle_as_int(reduct, argv[0]); \
        for (size_t i = 1; i < argc; i++) \
        { \
            res _op## = reduct_handle_as_int(reduct, argv[i]); \
        } \
        return REDUCT_HANDLE_FROM_NUMBER((double)res); \
    }

#define REDUCT_BUILD_NATIVE_COMPARE(_name, _op) \
    static reduct_handle_t reduct_build_##_name##_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        if (argc < 2) \
        { \
            return REDUCT_HANDLE_TRUE(); \
        } \
        for (size_t i = 0; i < argc - 1; i++) \
        { \
            if (!(reduct_handle_compare(reduct, &argv[i], &argv[i + 1]) _op 0)) \
            { \
                return REDUCT_HANDLE_FALSE(); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    }

REDUCT_BUILD_NATIVE_ARITH(add, +, 0)
REDUCT_BUILD_NATIVE_ARITH(mul, *, 1)
REDUCT_BUILD_NATIVE_ARITH(sub, -, 0)

REDUCT_BUILD_NATIVE_BITWISE(band, &)
REDUCT_BUILD_NATIVE_BITWISE(bor, |)
REDUCT_BUILD_NATIVE_BITWISE(bxor, ^)

REDUCT_BUILD_NATIVE_COMPARE(eq, ==)
REDUCT_BUILD_NATIVE_COMPARE(neq, !=)
REDUCT_BUILD_NATIVE_COMPARE(lt, <)
REDUCT_BUILD_NATIVE_COMPARE(le, <=)
REDUCT_BUILD_NATIVE_COMPARE(gt, >)
REDUCT_BUILD_NATIVE_COMPARE(ge, >=)

REDUCT_BUILD_NATIVE_LOGIC(and, false)
REDUCT_BUILD_NATIVE_LOGIC(or, true)

static reduct_handle_t reduct_build_div_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc >= 1, "/: expected at least 1 argument(s), got %zu", (size_t)argc);
    if (argc == 1)
    {
        reduct_handle_t res;
        reduct_handle_t one = REDUCT_HANDLE_FROM_NUMBER(1.0);
        REDUCT_HANDLE_DIV_FAST(reduct, &res, one, argv[0]);
        return res;
    }
    reduct_handle_t res = argv[0];
    for (size_t i = 1; i < argc; i++)
    {
        REDUCT_HANDLE_DIV_FAST(reduct, &res, res, argv[i]);
    }
    return res;
}

static reduct_handle_t reduct_build_mod_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 2, "%%: expected 2 argument(s), got %zu", (size_t)argc);
    reduct_handle_t result;
    REDUCT_HANDLE_MOD_FAST(reduct, &result, argv[0], argv[1]);
    return result;
}

static reduct_handle_t reduct_build_inc_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "++: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_NUMBER(1.0);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, argv[0], one, +);
    return res;
}

static reduct_handle_t reduct_build_dec_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "--: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_NUMBER(1.0);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, argv[0], one, -);
    return res;
}

static reduct_handle_t reduct_build_bnot_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "~: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_FROM_NUMBER((double)(~reduct_handle_as_int(reduct, argv[0])));
}

static reduct_handle_t reduct_build_shl_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 2, "<<: expected 2 argument(s), got %zu", (size_t)argc);
    int64_t left = reduct_handle_as_int(reduct, argv[0]);
    int64_t right = reduct_handle_as_int(reduct, argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_NUMBER_WIDTH)
    {
        REDUCT_ERROR_THROW(reduct, "<<: shift amount must be 0-%lu, got %lld",
            (unsigned long)REDUCT_HANDLE_NUMBER_WIDTH, (long long)right);
    }
    return REDUCT_HANDLE_FROM_NUMBER((double)(left << right));
}

static reduct_handle_t reduct_build_shr_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 2, ">>: expected 2 argument(s), got %zu", (size_t)argc);
    int64_t left = reduct_handle_as_int(reduct, argv[0]);
    int64_t right = reduct_handle_as_int(reduct, argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_NUMBER_WIDTH)
    {
        REDUCT_ERROR_THROW(reduct, ">>: shift amount must be 0-%lu, got %lld",
            (unsigned long)REDUCT_HANDLE_NUMBER_WIDTH - 1, (long long)right);
    }
    return REDUCT_HANDLE_FROM_NUMBER((double)(left >> right));
}

static reduct_handle_t reduct_build_do_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    if (argc == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }
    return argv[argc - 1];
}

static reduct_handle_t reduct_build_not_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "not: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_IS_TRUTHY(argv[0]) ? REDUCT_HANDLE_FALSE() : REDUCT_HANDLE_TRUE();
}

static reduct_rvsdg_origin_t* reduct_build_add(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_ADD);
}

static reduct_rvsdg_origin_t* reduct_build_sub(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_SUB);
}

static reduct_rvsdg_origin_t* reduct_build_mul(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_MUL);
}

static reduct_rvsdg_origin_t* reduct_build_div(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_DIV);
}

static reduct_rvsdg_origin_t* reduct_build_mod(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_MOD);
}

static reduct_rvsdg_origin_t* reduct_build_bit_and(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_BAND);
}

static reduct_rvsdg_origin_t* reduct_build_bit_or(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_BOR);
}

static reduct_rvsdg_origin_t* reduct_build_bit_xor(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_BXOR);
}

static reduct_rvsdg_origin_t* reduct_build_bit_not(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "~: expected 1 argument");
    return reduct_rvsdg_node_new_simple_unary(builder->reduct, builder->scope->region, REDUCT_OPCODE_BNOT,
        reduct_build_handle(builder, reduct_list_second(builder->reduct, list)))
        ->firstOutput;
}

static reduct_rvsdg_origin_t* reduct_build_bit_shl(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_SHL);
}

static reduct_rvsdg_origin_t* reduct_build_bit_shr(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_variadic_op(builder, list, REDUCT_OPCODE_SHR);
}

static reduct_rvsdg_origin_t* reduct_build_equal(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_EQ);
}

static reduct_rvsdg_origin_t* reduct_build_not_equal(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_NEQ);
}

static reduct_rvsdg_origin_t* reduct_build_less(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_LT);
}

static reduct_rvsdg_origin_t* reduct_build_less_equal(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_LE);
}

static reduct_rvsdg_origin_t* reduct_build_greater(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_GT);
}

static reduct_rvsdg_origin_t* reduct_build_greater_equal(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_chained_comparison(builder, list, REDUCT_OPCODE_GE);
}

static reduct_native_t reductIntrinsics[] = {
    {"quote", NULL, reduct_build_quote},
    {"recur", NULL, reduct_build_recur},
    {"list", reduct_build_list_native, reduct_build_list},
    {"do", reduct_build_do_native, reduct_build_do},
    {"lambda", NULL, reduct_build_lambda},
    {"->", NULL, reduct_build_thread},
    {"def", NULL, reduct_build_def},
    {"if", NULL, reduct_build_if},
    {"cond", NULL, reduct_build_cond},
    {"match", NULL, reduct_build_match},
    {"and", reduct_build_and_native, reduct_build_and},
    {"or", reduct_build_or_native, reduct_build_or},
    {"not", reduct_build_not_native, reduct_build_not},
    {"+", reduct_build_add_native, reduct_build_add},
    {"-", reduct_build_sub_native, reduct_build_sub},
    {"*", reduct_build_mul_native, reduct_build_mul},
    {"/", reduct_build_div_native, reduct_build_div},
    {"%", reduct_build_mod_native, reduct_build_mod},
    {"++", reduct_build_inc_native, reduct_build_inc},
    {"--", reduct_build_dec_native, reduct_build_dec},
    {"&", reduct_build_band_native, reduct_build_bit_and},
    {"|", reduct_build_bor_native, reduct_build_bit_or},
    {"^", reduct_build_bxor_native, reduct_build_bit_xor},
    {"~", reduct_build_bnot_native, reduct_build_bit_not},
    {"<<", reduct_build_shl_native, reduct_build_bit_shl},
    {">>", reduct_build_shr_native, reduct_build_bit_shr},
    {"==", reduct_build_eq_native, reduct_build_equal},
    {"!=", reduct_build_neq_native, reduct_build_not_equal},
    {"<", reduct_build_lt_native, reduct_build_less},
    {"<=", reduct_build_le_native, reduct_build_less_equal},
    {">", reduct_build_gt_native, reduct_build_greater},
    {">=", reduct_build_ge_native, reduct_build_greater_equal},
};

static inline reduct_rvsdg_origin_t* reduct_build_dispatch_atom(reduct_builder_t* builder, reduct_handle_t handle)
{
    assert(builder != NULL);

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(handle))
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, handle)->firstOutput;
    }

    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED || reduct_atom_is_intrinsic(builder->reduct, atom) ||
        reduct_atom_is_native(builder->reduct, atom))
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, handle)->firstOutput;
    }

    reduct_builder_local_t* local = reduct_builder_resolve_local(builder, builder->scope, atom);
    if (local != NULL)
    {
        assert(local->value != NULL);
        return local->value;
    }

    switch (atom->length)
    {
    case 1:
        if (atom->string[0] == 'e')
        {
            return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_E())
                ->firstOutput;
        }
        break;
    case 2:
        if (memcmp(atom->string, "pi", 2) == 0)
        {
            return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_PI())
                ->firstOutput;
        }
        break;
    case 3:
        if (memcmp(atom->string, "nil", 3) == 0)
        {
            return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
                REDUCT_HANDLE_NIL(builder->reduct))
                ->firstOutput;
        }
        break;
    case 4:
        if (memcmp(atom->string, "true", 4) == 0)
        {
            return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_TRUE())
                ->firstOutput;
        }
        break;
    case 5:
        if (memcmp(atom->string, "false", 5) == 0)
        {
            return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FALSE())
                ->firstOutput;
        }
        break;
    }

    REDUCT_ERROR_COMPILE(builder, handle, "unknown symbol '%.*s'", atom->length, atom->string);
}

static inline bool reduct_builder_is_data(reduct_builder_t* builder, reduct_handle_t handle)
{
    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(handle))
    {
        return true;
    }

    if (REDUCT_HANDLE_IS_ATOM(handle))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
        return (atom->flags & REDUCT_ATOM_FLAG_QUOTED) != 0;
    }

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        if (list->length == 0)
        {
            return true;
        }

        reduct_handle_t head = reduct_list_first(builder->reduct, list);
        return reduct_builder_is_data(builder, head);
    }

    return false;
}

static inline reduct_rvsdg_origin_t* reduct_build_dispatch_list(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    if (list->length == 0)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
            ->firstOutput;
    }

    reduct_handle_t head = reduct_list_first(builder->reduct, list);
    if (reduct_builder_is_data(builder, head))
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, head)->firstOutput;
    }

    if (REDUCT_HANDLE_IS_INTRINSIC(builder->reduct, head))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(head);
        return atom->intrinsic(builder, list);
    }

    reduct_rvsdg_origin_t* callable = reduct_build_handle(builder, head);
    reduct_rvsdg_node_t* call = reduct_rvsdg_node_new_call(builder->reduct, builder->scope->region, callable);

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 1);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, chunk.handles[i]);
            reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(builder->reduct, call);
            reduct_rvsdg_edge_connect(builder->reduct, arg, input);
        }
    }

    return call->firstOutput;
}

REDUCT_API reduct_handle_t reduct_build(reduct_t* reduct, reduct_handle_t ast)
{
    if (!REDUCT_HANDLE_IS_LIST(ast))
    {
        REDUCT_ERROR_THROW(reduct, "expected list as root of AST, got %s", REDUCT_HANDLE_GET_TYPE_STRING(ast));
    }

    if (!reduct->hasRegisteredIntrinsics)
    {
        reduct_native_register(reduct, reductIntrinsics, sizeof(reductIntrinsics) / sizeof(reductIntrinsics[0]));
        reduct->hasRegisteredIntrinsics = true;
    }

    reduct_builder_t builder = {
        .reduct = reduct,
        .lastItem = REDUCT_HANDLE_TO_ITEM(ast),
    };

    reduct_rvsdg_node_t* lambda = reduct_rvsdg_node_new_lambda(builder.reduct, NULL);

    reduct_builder_scope_t scope = {.region = lambda->firstRegion, .lambdaNode = lambda};
    builder.scope = &scope;

    reduct_rvsdg_origin_t* origin = reduct_build_handle(&builder, ast);
    reduct_rvsdg_edge_connect(builder.reduct, origin, lambda->firstRegion->firstResult);

    return REDUCT_HANDLE_FROM_RVSDG_NODE(lambda);
}

REDUCT_API reduct_rvsdg_origin_t* reduct_build_handle(reduct_builder_t* builder, reduct_handle_t handle)
{
    assert(builder != NULL);

    reduct_item_t* previousItem = builder->lastItem;
    if (REDUCT_HANDLE_IS_ITEM(handle))
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
        if (item->inputId != REDUCT_INPUT_ID_NONE)
        {
            builder->lastItem = item;
        }
    }

    reduct_rvsdg_origin_t* out;
    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        out = reduct_build_dispatch_atom(builder, handle);
    }
    else if (REDUCT_HANDLE_IS_LIST(handle))
    {
        out = reduct_build_dispatch_list(builder, REDUCT_HANDLE_TO_LIST(handle));
    }
    else
    {
        REDUCT_ERROR_COMPILE_LAST(builder, "unexpected %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    }

    builder->lastItem = previousItem;
    return out;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_build_generic_list(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx)
{
    if (!REDUCT_HANDLE_IS_LIST(list))
    {
        REDUCT_ERROR_COMPILE(builder, list, "expected list, got %s", REDUCT_HANDLE_GET_TYPE_STRING(list));
    }
    reduct_list_t* listPtr = REDUCT_HANDLE_TO_LIST(list);

    reduct_rvsdg_node_t* node =
        reduct_rvsdg_node_new_simple_opcode(builder->reduct, builder->scope->region, REDUCT_OPCODE_LIST);

    reduct_list_iter_t iter = REDUCT_LIST_ITER(listPtr);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        if (iter.index < startIdx)
        {
            continue;
        }

        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            size_t idx = baseIdx + i;
            if (idx < startIdx)
            {
                continue;
            }

            reduct_rvsdg_origin_t* input = reduct_build_handle(builder, chunk.handles[i]);
            reduct_rvsdg_user_t* user = reduct_rvsdg_node_add_input(builder->reduct, node);
            reduct_rvsdg_edge_connect(builder->reduct, input, user);
        }
    }

    return node->firstOutput;
}

REDUCT_API reduct_rvsdg_origin_t* reduct_build_generic_block(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx)
{
    reduct_rvsdg_origin_t* last = NULL;

    if (!REDUCT_HANDLE_IS_LIST(list))
    {
        REDUCT_ERROR_COMPILE(builder, list, "expected list, got %s", REDUCT_HANDLE_GET_TYPE_STRING(list));
    }

    reduct_list_iter_t iter = REDUCT_LIST_ITER(REDUCT_HANDLE_TO_LIST(list));
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        if (iter.index < startIdx)
        {
            continue;
        }

        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            size_t idx = baseIdx + i;
            if (idx < startIdx)
            {
                continue;
            }

            reduct_handle_t handle = chunk.handles[i];
            last = reduct_build_handle(builder, handle);
        }
    }

    return last;
}