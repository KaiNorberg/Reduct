#include "reduct/error.h"
#include "reduct/inst.h"
#include <reduct/build.h>
#include <reduct/core.h>
#include <reduct/handle.h>
#include <reduct/rvsdg.h>
#include <reduct/standard.h>

typedef struct reduct_builder_local
{
    reduct_atom_t* key;
    reduct_rvsdg_origin_t* value;
} reduct_builder_local_t;

typedef struct reduct_builder_scope
{
    reduct_builder_local_t locals[REDUCT_REGISTER_MAX];
    uint64_t localAmount;
    reduct_rvsdg_region_t* region;
    struct reduct_builder_scope* parent;
    reduct_rvsdg_node_t* lambdaNode;
} reduct_builder_scope_t;

typedef struct reduct_builder
{
    reduct_t* reduct;
    reduct_item_t* lastItem;
    reduct_builder_scope_t* scope;
    reduct_atom_t* eAtom;
    reduct_atom_t* piAtom;
    reduct_atom_t* nilAtom;
    reduct_atom_t* trueAtom;
    reduct_atom_t* falseAtom;
} reduct_builder_t;

#define REDUCT_BUILDER_RESOLVE_ORIGIN_MAX_DEPTH 128

static reduct_rvsdg_origin_t* reduct_build_handle(reduct_builder_t* builder, reduct_handle_t handle);
static reduct_rvsdg_origin_t* reduct_build_generic_list(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx, reduct_opcode_t op);
static reduct_rvsdg_origin_t* reduct_build_generic_block(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx);

static inline reduct_builder_local_t* reduct_builder_add_local_to_scope(reduct_builder_t* builder,
    reduct_builder_scope_t* scope, struct reduct_atom* key, reduct_rvsdg_origin_t* value)
{
    assert(builder != NULL);
    assert(key != NULL);
    assert(scope != NULL);

    key = reduct_atom_ensure_interned(builder->reduct, key);
    for (size_t i = 0; i < scope->localAmount; i++)
    {
        if (scope->locals[i].key == key)
        {
            scope->locals[i].value = value;
            return &scope->locals[i];
        }
    }

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

static inline void reduct_builder_enter_scope(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    struct reduct_rvsdg_region* region, struct reduct_rvsdg_node* lambdaNode)
{
    assert(builder != NULL);
    assert(scope != NULL);

    memset(scope, 0, sizeof(reduct_builder_scope_t));
    scope->region = region;
    scope->parent = builder->scope;
    scope->lambdaNode = lambdaNode != NULL ? lambdaNode : builder->scope->lambdaNode;
    builder->scope = scope;
}

static inline void reduct_builder_exit_scope(reduct_builder_t* builder)
{
    assert(builder != NULL);
    assert(builder->scope != NULL);
    builder->scope = builder->scope->parent;
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

    reduct_rvsdg_region_t* chain[REDUCT_BUILDER_RESOLVE_ORIGIN_MAX_DEPTH];
    int32_t depth = 0;
    reduct_rvsdg_region_t* curr = scope->region;
    reduct_rvsdg_region_t* target = scope->parent->region;

    while (curr != NULL && curr != target)
    {
        assert(depth < REDUCT_BUILDER_RESOLVE_ORIGIN_MAX_DEPTH);
        chain[depth++] = curr;
        if (curr->parent == NULL)
        {
            break;
        }
        curr = curr->parent->parent;
    }

    reduct_rvsdg_origin_t* val = outerValue;
    for (int32_t i = depth - 1; i >= 0; i--)
    {
        val = reduct_rvsdg_region_lift_origin(builder->reduct, chain[i], val);
    }

    return val;
}

static reduct_builder_local_t* reduct_builder_resolve_local(reduct_builder_t* builder, reduct_builder_scope_t* scope,
    struct reduct_atom* key)
{
    assert(builder != NULL);
    assert(scope != NULL);
    assert(key != NULL);

    key = reduct_atom_ensure_interned(builder->reduct, key);
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
        reduct_rvsdg_node_t* lambda = scope->lambdaNode;

        if (!reduct_rvsdg_node_is_inside_phi(lambda))
        {
            reduct_rvsdg_node_phi_wrap_lambda(builder->reduct, lambda);
        }

        reduct_rvsdg_node_t* phi = lambda->parent->parent;

        reduct_rvsdg_origin_t* callable = reduct_builder_resolve_origin(builder, scope, phi->output);
        return reduct_builder_add_local_to_scope(builder, scope, key, callable);
    }

    reduct_rvsdg_origin_t* resultArg = reduct_builder_resolve_origin(builder, scope, outer->value);
    return reduct_builder_add_local_to_scope(builder, scope, key, resultArg);
}

static reduct_rvsdg_origin_t* reduct_build_variadic_op(reduct_builder_t* builder, reduct_list_t* list,
    reduct_opcode_t op)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 2, "operator: expected at least 1 argument");
    reduct_rvsdg_origin_t* acc = reduct_build_handle(builder, list->handles[1]);

    if (list->length == 2)
    {
        if (op == REDUCT_OPCODE_SUB)
        {
            reduct_rvsdg_origin_t* zero = reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
                REDUCT_HANDLE_FROM_NUMBER(0.0))
                                              ->output;
            return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_SUB, zero,
                acc)
                ->output;
        }
        if (op == REDUCT_OPCODE_DIV)
        {
            reduct_rvsdg_origin_t* one = reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
                REDUCT_HANDLE_FROM_NUMBER(1.0))
                                             ->output;
            return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_DIV, one,
                acc)
                ->output;
        }
        return acc;
    }

    for (size_t i = 2; i < list->length; i++)
    {
        reduct_rvsdg_origin_t* next = reduct_build_handle(builder, list->handles[i]);
        acc = reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, op, acc, next)->output;
    }
    return acc;
}

static reduct_rvsdg_origin_t* reduct_build_quote(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "quote: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, list->handles[1])->output;
}

static reduct_rvsdg_origin_t* reduct_build_recur(reduct_builder_t* builder, reduct_list_t* list)
{
    reduct_rvsdg_node_t* lambda = builder->scope->lambdaNode;
    if (!reduct_rvsdg_node_is_inside_phi(lambda))
    {
        reduct_rvsdg_node_phi_wrap_lambda(builder->reduct, lambda);
    }

    reduct_rvsdg_node_t* phi = lambda->parent->parent;
    reduct_rvsdg_origin_t* callable = reduct_builder_resolve_origin(builder, builder->scope, phi->output);

    reduct_rvsdg_node_t* call = reduct_rvsdg_node_new_call(builder->reduct, builder->scope->region, callable);

    for (size_t i = 1; i < list->length; i++)
    {
        reduct_handle_t arg = list->handles[i];
        reduct_rvsdg_origin_t* argOrigin = reduct_build_handle(builder, arg);
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(builder->reduct, call);
        reduct_rvsdg_edge_connect(builder->reduct, argOrigin, input);
    }

    return call->output;
}

static reduct_handle_t reduct_build_list_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return REDUCT_HANDLE_CREATE_HANDLES(reduct, argc, argv);
}

static reduct_rvsdg_origin_t* reduct_build_list(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_generic_list(builder, REDUCT_HANDLE_FROM_LIST(list), 1, REDUCT_OPCODE_LIST);
}

static reduct_rvsdg_origin_t* reduct_build_do(reduct_builder_t* builder, reduct_list_t* list)
{
    return reduct_build_generic_block(builder, REDUCT_HANDLE_FROM_LIST(list), 1);
}

static reduct_rvsdg_origin_t* reduct_build_lambda(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "lambda: expected 2 arguments, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t args = list->handles[1];
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_LIST(args), "lambda: parameter list must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(args));

    reduct_rvsdg_node_t* lambda = reduct_rvsdg_node_new_lambda(builder->reduct, builder->scope->region);
    reduct_item_t* lambdaItem = REDUCT_CONTAINER_OF(lambda, reduct_item_t, list);
    reduct_item_t* listItem = REDUCT_CONTAINER_OF(list, reduct_item_t, list);
    lambdaItem->moduleId = listItem->moduleId;
    lambdaItem->modulePos = listItem->modulePos;

    reduct_builder_scope_t scope;
    reduct_builder_enter_scope(builder, &scope, lambda->firstRegion, lambda);

    reduct_list_t* argsList = REDUCT_HANDLE_TO_LIST(args);
    for (size_t i = 0; i < argsList->length; i++)
    {
        reduct_handle_t param = argsList->handles[i];
        REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_ATOM(param), "lambda: parameter must be an atom, got %s",
            REDUCT_HANDLE_GET_TYPE_STRING(param));

        reduct_rvsdg_origin_t* value = reduct_rvsdg_region_add_argument(builder->reduct, lambda->firstRegion);

        reduct_atom_t* key = REDUCT_HANDLE_TO_ATOM(param);
        if (key->length > 0 && key->string[0] == '*')
        {
            lambda->flags |= REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC;
            reduct_atom_t* nameOnly = reduct_atom_substr(builder->reduct, key, 1, key->length - 1);
            nameOnly = reduct_atom_ensure_interned(builder->reduct, nameOnly);
            reduct_builder_add_local(builder, nameOnly, value);
        }

        reduct_builder_add_local(builder, key, value);
    }

    reduct_rvsdg_origin_t* origin = reduct_build_generic_block(builder, REDUCT_HANDLE_FROM_LIST(list), 2);
    reduct_rvsdg_edge_connect(builder->reduct, origin, lambda->firstRegion->result);

    reduct_builder_exit_scope(builder);

    if (reduct_rvsdg_node_is_inside_phi(lambda))
    {
        return lambda->parent->parent->output;
    }

    return lambda->output;
}

static reduct_rvsdg_origin_t* reduct_build_thread(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 2, "->: expected at least 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t current = list->handles[1];
    for (size_t i = 2; i < list->length; i++)
    {
        reduct_list_t* next;
        reduct_handle_t step = list->handles[i];
        if (REDUCT_HANDLE_IS_ATOM(step))
        {
            next = reduct_list_new(builder->reduct, 2);
            reduct_item_t* nextItem = REDUCT_CONTAINER_OF(next, reduct_item_t, list);
            reduct_item_t* stepItem = REDUCT_HANDLE_TO_ITEM(step);

            nextItem->moduleId = stepItem->moduleId;
            nextItem->modulePos = stepItem->modulePos;

            next->handles[0] = step;
            next->handles[1] = current;

            current = REDUCT_HANDLE_FROM_LIST(next);
        }
        else if (REDUCT_HANDLE_IS_LIST(step))
        {
            reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(step);
            REDUCT_ERROR_COMPILE_ASSERT(builder, stepList->length > 0, "->: step cannot be an empty list");

            next = reduct_list_new(builder->reduct, stepList->length + 1);
            reduct_item_t* nextItem = REDUCT_CONTAINER_OF(next, reduct_item_t, list);
            reduct_item_t* stepItem = REDUCT_HANDLE_TO_ITEM(step);

            nextItem->moduleId = stepItem->moduleId;
            nextItem->modulePos = stepItem->modulePos;

            next->handles[0] = stepList->handles[0];
            next->handles[1] = current;
            if (stepList->length > 1)
            {
                memcpy(next->handles + 2, stepList->handles + 1, (stepList->length - 1) * sizeof(reduct_handle_t));
            }

            current = REDUCT_HANDLE_FROM_LIST(next);
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

    reduct_handle_t name = list->handles[1];
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_ATOM(name), "def: first argument must be a symbol, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(name));

    reduct_builder_local_t* local = reduct_builder_add_local(builder, REDUCT_HANDLE_TO_ATOM(name), NULL);
    local->value = reduct_build_handle(builder, list->handles[2]);
    return local->value;
}

static reduct_rvsdg_origin_t* reduct_build_import(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 1 || list->length == 2 || list->length == 3,
        "import: expected 1, 2, or 3 arguments, got %zu", (size_t)list->length - 1);

    reduct_handle_t path = list->handles[1];
    reduct_handle_t compiler = (list->length == 2) ? list->handles[2] : REDUCT_HANDLE_NIL(builder->reduct);
    reduct_handle_t compilerArgs = (list->length == 3) ? list->handles[3] : REDUCT_HANDLE_NIL(builder->reduct);
    reduct_handle_t ast = reduct_module_import(builder->reduct, path, compiler, compilerArgs);

    return reduct_build_generic_block(builder, ast, 0);
}

static reduct_rvsdg_origin_t* reduct_build_if(reduct_builder_t* builder, reduct_list_t* list)
{
    assert(builder != NULL);
    assert(list != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 3 || list->length == 4,
        "if: expected 2 or 3 arguments, got %zu", (size_t)list->length - 1);

    reduct_handle_t cond = list->handles[1];
    reduct_handle_t then = list->handles[2];
    reduct_handle_t els = (list->length == 4) ? list->handles[3] : REDUCT_HANDLE_NIL(builder->reduct);

    reduct_rvsdg_origin_t* condOrigin = reduct_build_handle(builder, cond);
    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);

    reduct_rvsdg_edge_connect(builder->reduct, condOrigin, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_t* elseRegion = gamma->firstRegion;
    reduct_rvsdg_region_t* thenRegion = gamma->firstRegion->next;

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, elseRegion, NULL);

        reduct_rvsdg_origin_t* result = reduct_build_handle(builder, els);
        reduct_rvsdg_edge_connect(builder->reduct, result, elseRegion->result);
        reduct_builder_exit_scope(builder);
    }

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, thenRegion, NULL);

        reduct_rvsdg_origin_t* result = reduct_build_handle(builder, then);
        reduct_rvsdg_edge_connect(builder->reduct, result, thenRegion->result);
        reduct_builder_exit_scope(builder);
    }

    return gamma->output;
}

static reduct_list_t* reduct_build_get_pair(reduct_builder_t* builder, reduct_handle_t handle, const char* name)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, REDUCT_HANDLE_IS_LIST(handle), "%s: clause must be a list", name);
    reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(handle);
    REDUCT_ERROR_COMPILE_ASSERT(builder, pair->length == 2, "%s: clause must have 2 items", name);
    return pair;
}

static reduct_rvsdg_origin_t* reduct_build_not(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "not: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, list->handles[1]);

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);
    reduct_rvsdg_edge_connect(builder->reduct, arg, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion, REDUCT_HANDLE_TRUE())->output,
        gamma->firstRegion->result);
    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion->next,
            REDUCT_HANDLE_FALSE(builder->reduct))
            ->output,
        gamma->firstRegion->next->result);

    return gamma->output;
}

static reduct_rvsdg_origin_t* reduct_build_generic_and_or(reduct_builder_t* builder, reduct_list_t* list, size_t index,
    bool isOr)
{
    reduct_rvsdg_origin_t* current = reduct_build_handle(builder, list->handles[index]);
    if (index == list->length - 1)
    {
        return current;
    }

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);
    reduct_rvsdg_edge_connect(builder->reduct, current, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_region_t* falsyRegion = gamma->firstRegion;
    reduct_rvsdg_region_t* truthyRegion = gamma->firstRegion->next;

    if (!isOr)
    {
        {
            reduct_builder_scope_t scope;
            reduct_builder_enter_scope(builder, &scope, falsyRegion, NULL);
            reduct_rvsdg_edge_connect(builder->reduct, reduct_builder_resolve_origin(builder, &scope, current),
                falsyRegion->result);
            reduct_builder_exit_scope(builder);
        }
        {
            reduct_builder_scope_t scope;
            reduct_builder_enter_scope(builder, &scope, truthyRegion, NULL);
            reduct_rvsdg_origin_t* res = reduct_build_generic_and_or(builder, list, index + 1, false);
            reduct_rvsdg_edge_connect(builder->reduct, res, truthyRegion->result);
            reduct_builder_exit_scope(builder);
        }
    }
    else
    {
        {
            reduct_builder_scope_t scope;
            reduct_builder_enter_scope(builder, &scope, falsyRegion, NULL);
            reduct_rvsdg_origin_t* res = reduct_build_generic_and_or(builder, list, index + 1, true);
            reduct_rvsdg_edge_connect(builder->reduct, res, falsyRegion->result);
            reduct_builder_exit_scope(builder);
        }
        {
            reduct_builder_scope_t scope;
            reduct_builder_enter_scope(builder, &scope, truthyRegion, NULL);
            reduct_rvsdg_edge_connect(builder->reduct, reduct_builder_resolve_origin(builder, &scope, current),
                truthyRegion->result);
            reduct_builder_exit_scope(builder);
        }
    }

    return gamma->output;
}

static reduct_rvsdg_origin_t* reduct_build_and(reduct_builder_t* builder, reduct_list_t* list)
{
    if (list->length == 1)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_FALSE(builder->reduct))
            ->output;
    }
    return reduct_build_generic_and_or(builder, list, 1, false);
}

static reduct_rvsdg_origin_t* reduct_build_or(reduct_builder_t* builder, reduct_list_t* list)
{
    if (list->length == 1)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_FALSE(builder->reduct))
            ->output;
    }
    return reduct_build_generic_and_or(builder, list, 1, true);
}

static reduct_rvsdg_origin_t* reduct_build_bit_not(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "~: expected 1 argument");
    return reduct_rvsdg_node_new_simple_unary(builder->reduct, builder->scope->region, REDUCT_OPCODE_BNOT,
        reduct_build_handle(builder, list->handles[1]))
        ->output;
}

static reduct_rvsdg_origin_t* reduct_build_inc(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "++: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, list->handles[1]);
    reduct_rvsdg_origin_t* one =
        reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FROM_NUMBER(1.0))
            ->output;
    return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_ADD, arg, one)
        ->output;
}

static reduct_rvsdg_origin_t* reduct_build_dec(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length == 2, "--: expected 1 argument");
    reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, list->handles[1]);
    reduct_rvsdg_origin_t* one =
        reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_FROM_NUMBER(1.0))
            ->output;
    return reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_SUB, arg, one)
        ->output;
}

static reduct_rvsdg_origin_t* reduct_build_comparison_internal(reduct_builder_t* builder, reduct_list_t* list,
    size_t index, reduct_opcode_t op, reduct_rvsdg_origin_t* left)
{
    reduct_rvsdg_origin_t* right = reduct_build_handle(builder, list->handles[index]);
    reduct_rvsdg_origin_t* cmp =
        reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, op, left, right)->output;

    if (index == list->length - 1)
    {
        return cmp;
    }

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);
    reduct_rvsdg_edge_connect(builder->reduct, cmp, reduct_rvsdg_node_get_input(gamma, 0));

    reduct_rvsdg_edge_connect(builder->reduct,
        reduct_rvsdg_node_new_simple_constant(builder->reduct, gamma->firstRegion, REDUCT_HANDLE_FALSE(builder->reduct))
            ->output,
        gamma->firstRegion->result);

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, gamma->firstRegion->next, NULL);
        reduct_rvsdg_origin_t* next = reduct_build_comparison_internal(builder, list, index + 1, op,
            reduct_builder_resolve_origin(builder, &scope, right));
        reduct_rvsdg_edge_connect(builder->reduct, next, gamma->firstRegion->next->result);
        reduct_builder_exit_scope(builder);
    }

    return gamma->output;
}

static reduct_rvsdg_origin_t* reduct_build_chained_comparison(reduct_builder_t* builder, reduct_list_t* list,
    reduct_opcode_t op)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "comparison: expected at least 2 arguments");
    reduct_rvsdg_origin_t* left = reduct_build_handle(builder, list->handles[1]);
    return reduct_build_comparison_internal(builder, list, 2, op, left);
}

static reduct_rvsdg_origin_t* reduct_build_cond_internal(reduct_builder_t* builder, reduct_list_t* list, size_t index)
{
    if (index == list->length)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
            ->output;
    }

    reduct_list_t* pair = reduct_build_get_pair(builder, list->handles[index], "cond");
    reduct_rvsdg_origin_t* cond = reduct_build_handle(builder, pair->handles[0]);

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);
    reduct_rvsdg_edge_connect(builder->reduct, cond, reduct_rvsdg_node_get_input(gamma, 0));

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, gamma->firstRegion, NULL);
        reduct_rvsdg_origin_t* res = reduct_build_cond_internal(builder, list, index + 1);
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->result);
        reduct_builder_exit_scope(builder);
    }

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, gamma->firstRegion->next, NULL);
        reduct_rvsdg_origin_t* res = reduct_build_handle(builder, pair->handles[1]);
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->next->result);
        reduct_builder_exit_scope(builder);
    }

    return gamma->output;
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
        return reduct_build_handle(builder, list->handles[index]);
    }

    reduct_list_t* pair = reduct_build_get_pair(builder, list->handles[index], "match");
    reduct_rvsdg_origin_t* caseVal = reduct_build_handle(builder, pair->handles[0]);

    reduct_rvsdg_origin_t* cmp =
        reduct_rvsdg_node_new_simple_binary(builder->reduct, builder->scope->region, REDUCT_OPCODE_EQ, target, caseVal)
            ->output;

    reduct_rvsdg_node_t* gamma = reduct_rvsdg_node_new_gamma(builder->reduct, builder->scope->region);
    reduct_rvsdg_edge_connect(builder->reduct, cmp, reduct_rvsdg_node_get_input(gamma, 0));

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, gamma->firstRegion, NULL);
        reduct_rvsdg_origin_t* res = reduct_build_match_internal(builder, list, index + 1,
            reduct_builder_resolve_origin(builder, &scope, target));
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->result);
        reduct_builder_exit_scope(builder);
    }

    {
        reduct_builder_scope_t scope;
        reduct_builder_enter_scope(builder, &scope, gamma->firstRegion->next, NULL);
        reduct_rvsdg_origin_t* res = reduct_build_handle(builder, pair->handles[1]);
        reduct_rvsdg_edge_connect(builder->reduct, res, gamma->firstRegion->next->result);
        reduct_builder_exit_scope(builder);
    }

    return gamma->output;
}

static reduct_rvsdg_origin_t* reduct_build_match(reduct_builder_t* builder, reduct_list_t* list)
{
    REDUCT_ERROR_COMPILE_ASSERT(builder, list->length >= 3, "match: expected at least target and one case");
    reduct_rvsdg_origin_t* target = reduct_build_handle(builder, list->handles[1]);
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
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_FALSE(reduct); \
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
            if (!(reduct_handle_compare(reduct, argv[i], argv[i + 1]) _op 0)) \
            { \
                return REDUCT_HANDLE_FALSE(reduct); \
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

static reduct_handle_t reduct_build_not_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "not: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_FROM_BOOL(reduct, !REDUCT_HANDLE_IS_TRUTHY(argv[0]));
}

#define REDUCT_BUILD_INTRINSIC_VARIADIC(_name, _opcode) \
    static reduct_rvsdg_origin_t* reduct_build_##_name(reduct_builder_t* builder, reduct_list_t* list) \
    { \
        return reduct_build_variadic_op(builder, list, _opcode); \
    }

REDUCT_BUILD_INTRINSIC_VARIADIC(add, REDUCT_OPCODE_ADD)
REDUCT_BUILD_INTRINSIC_VARIADIC(sub, REDUCT_OPCODE_SUB)
REDUCT_BUILD_INTRINSIC_VARIADIC(mul, REDUCT_OPCODE_MUL)
REDUCT_BUILD_INTRINSIC_VARIADIC(div, REDUCT_OPCODE_DIV)
REDUCT_BUILD_INTRINSIC_VARIADIC(mod, REDUCT_OPCODE_MOD)
REDUCT_BUILD_INTRINSIC_VARIADIC(bit_and, REDUCT_OPCODE_BAND)
REDUCT_BUILD_INTRINSIC_VARIADIC(bit_or, REDUCT_OPCODE_BOR)
REDUCT_BUILD_INTRINSIC_VARIADIC(bit_xor, REDUCT_OPCODE_BXOR)
REDUCT_BUILD_INTRINSIC_VARIADIC(bit_shl, REDUCT_OPCODE_SHL)
REDUCT_BUILD_INTRINSIC_VARIADIC(bit_shr, REDUCT_OPCODE_SHR)

#define REDUCT_BUILD_INTRINSIC_COMPARISON(_name, _opcode) \
    static reduct_rvsdg_origin_t* reduct_build_##_name(reduct_builder_t* builder, reduct_list_t* list) \
    { \
        return reduct_build_chained_comparison(builder, list, _opcode); \
    }

REDUCT_BUILD_INTRINSIC_COMPARISON(equal, REDUCT_OPCODE_EQ)
REDUCT_BUILD_INTRINSIC_COMPARISON(not_equal, REDUCT_OPCODE_NEQ)
REDUCT_BUILD_INTRINSIC_COMPARISON(less, REDUCT_OPCODE_LT)
REDUCT_BUILD_INTRINSIC_COMPARISON(less_equal, REDUCT_OPCODE_LE)
REDUCT_BUILD_INTRINSIC_COMPARISON(greater, REDUCT_OPCODE_GT)
REDUCT_BUILD_INTRINSIC_COMPARISON(greater_equal, REDUCT_OPCODE_GE)

const reduct_native_t reductIntrinsics[UINT8_MAX + 1] = {
    {"quote", NULL, reduct_build_quote},
    {"do", NULL, reduct_build_do},
    {"lambda", NULL, reduct_build_lambda},
    {"->", NULL, reduct_build_thread},
    {"def", NULL, reduct_build_def},
    {"import", NULL, reduct_build_import},
    {"if", NULL, reduct_build_if},
    {"cond", NULL, reduct_build_cond},
    {"match", NULL, reduct_build_match},
    {"and", reduct_build_and_native, reduct_build_and},
    {"or", reduct_build_or_native, reduct_build_or},
    {"not", reduct_build_not_native, reduct_build_not},
    {"++", reduct_build_inc_native, reduct_build_inc},
    {"--", reduct_build_dec_native, reduct_build_dec},
    [REDUCT_OPCODE_RECUR] = {"recur", NULL, reduct_build_recur},
    [REDUCT_OPCODE_LIST] = {"list", reduct_build_list_native, reduct_build_list},
    [REDUCT_OPCODE_ADD] = {"+", reduct_build_add_native, reduct_build_add},
    [REDUCT_OPCODE_SUB] = {"-", reduct_build_sub_native, reduct_build_sub},
    [REDUCT_OPCODE_MUL] = {"*", reduct_build_mul_native, reduct_build_mul},
    [REDUCT_OPCODE_DIV] = {"/", reduct_build_div_native, reduct_build_div},
    [REDUCT_OPCODE_MOD] = {"%", reduct_build_mod_native, reduct_build_mod},
    [REDUCT_OPCODE_BAND] = {"&", reduct_build_band_native, reduct_build_bit_and},
    [REDUCT_OPCODE_BOR] = {"|", reduct_build_bor_native, reduct_build_bit_or},
    [REDUCT_OPCODE_BXOR] = {"^", reduct_build_bxor_native, reduct_build_bit_xor},
    [REDUCT_OPCODE_BNOT] = {"~", reduct_build_bnot_native, reduct_build_bit_not},
    [REDUCT_OPCODE_SHL] = {"<<", reduct_build_shl_native, reduct_build_bit_shl},
    [REDUCT_OPCODE_SHR] = {">>", reduct_build_shr_native, reduct_build_bit_shr},
    [REDUCT_OPCODE_EQ] = {"==", reduct_build_eq_native, reduct_build_equal},
    [REDUCT_OPCODE_NEQ] = {"!=", reduct_build_neq_native, reduct_build_not_equal},
    [REDUCT_OPCODE_LT] = {"<", reduct_build_lt_native, reduct_build_less},
    [REDUCT_OPCODE_LE] = {"<=", reduct_build_le_native, reduct_build_less_equal},
    [REDUCT_OPCODE_GT] = {">", reduct_build_gt_native, reduct_build_greater},
    [REDUCT_OPCODE_GE] = {">=", reduct_build_ge_native, reduct_build_greater_equal},
};

static inline reduct_rvsdg_origin_t* reduct_build_dispatch_atom(reduct_builder_t* builder, reduct_handle_t handle)
{
    assert(builder != NULL);

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(handle))
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, handle)->output;
    }

    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED || reduct_atom_is_intrinsic(builder->reduct, atom) ||
        reduct_atom_is_native(builder->reduct, atom))
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, handle)->output;
    }

    reduct_builder_local_t* local = reduct_builder_resolve_local(builder, builder->scope, atom);
    if (local != NULL)
    {
        REDUCT_ERROR_COMPILE_ASSERT(builder, local->value != NULL, "symbol '%.*s' is not yet defined", atom->length,
            atom->string);
        return local->value;
    }

    atom = reduct_atom_ensure_interned(builder->reduct, atom);
    if (atom == builder->eAtom)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_E())
            ->output;
    }
    if (atom == builder->piAtom)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_PI())
            ->output;
    }
    if (atom == builder->nilAtom)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
            ->output;
    }
    if (atom == builder->trueAtom)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region, REDUCT_HANDLE_TRUE())
            ->output;
    }
    if (atom == builder->falseAtom)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_FALSE(builder->reduct))
            ->output;
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

        reduct_handle_t head = list->handles[0];
        return reduct_builder_is_data(builder, head);
    }

    return false;
}

static inline reduct_rvsdg_origin_t* reduct_build_dispatch_list(reduct_builder_t* builder, reduct_handle_t handle)
{
    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    assert(builder != NULL);
    assert(list != NULL);

    if (list->length == 0)
    {
        return reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
            ->output;
    }

    reduct_handle_t head = list->handles[0];
    if (reduct_builder_is_data(builder, head))
    {
        return reduct_build_generic_list(builder, handle, 0, REDUCT_OPCODE_LIST);
    }

    if (REDUCT_HANDLE_IS_INTRINSIC(builder->reduct, head))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(head);
        return atom->intrinsic(builder, list);
    }

    reduct_rvsdg_origin_t* callable = reduct_build_handle(builder, head);
    reduct_rvsdg_node_t* call = reduct_rvsdg_node_new_call(builder->reduct, builder->scope->region, callable);

    for (uint32_t i = 1; i < list->length; i++)
    {
        reduct_rvsdg_origin_t* arg = reduct_build_handle(builder, list->handles[i]);
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_add_input(builder->reduct, call);
        reduct_rvsdg_edge_connect(builder->reduct, arg, input);
    }

    return call->output;
}

static reduct_rvsdg_origin_t* reduct_build_handle(reduct_builder_t* builder, reduct_handle_t handle)
{
    assert(builder != NULL);

    reduct_item_t* previousItem = builder->lastItem;
    if (REDUCT_HANDLE_IS_ITEM(handle))
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
        if (item->moduleId != REDUCT_MODULE_ID_NONE)
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
        out = reduct_build_dispatch_list(builder, handle);
    }
    else
    {
        REDUCT_ERROR_COMPILE_LAST(builder, "unexpected %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    }

    if (builder->lastItem != NULL)
    {
        reduct_item_t* outItem = REDUCT_CONTAINER_OF(out, reduct_item_t, rvsdgNode);
        reduct_item_t* ownerItem = out->ownerKind == REDUCT_RVSDG_OWNER_NODE
            ? REDUCT_CONTAINER_OF(out->node, reduct_item_t, rvsdgNode)
            : REDUCT_CONTAINER_OF(out->region, reduct_item_t, rvsdgRegion);
        outItem->moduleId = builder->lastItem->moduleId;
        outItem->modulePos = builder->lastItem->modulePos;
        ownerItem->moduleId = builder->lastItem->moduleId;
        ownerItem->modulePos = builder->lastItem->modulePos;
    }

    builder->lastItem = previousItem;
    return out;
}
static reduct_rvsdg_origin_t* reduct_build_generic_list(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx, reduct_opcode_t op)
{
    if (!REDUCT_HANDLE_IS_LIST(list))
    {
        REDUCT_ERROR_COMPILE(builder, list, "expected list, got %s", REDUCT_HANDLE_GET_TYPE_STRING(list));
    }
    reduct_list_t* listPtr = REDUCT_HANDLE_TO_LIST(list);

    reduct_rvsdg_node_t* node = reduct_rvsdg_node_new_simple_opcode(builder->reduct, builder->scope->region, op);

    for (uint32_t i = startIdx; i < listPtr->length; i++)
    {
        reduct_rvsdg_origin_t* input = reduct_build_handle(builder, listPtr->handles[i]);
        reduct_rvsdg_user_t* user = reduct_rvsdg_node_add_input(builder->reduct, node);
        reduct_rvsdg_edge_connect(builder->reduct, input, user);
    }

    return node->output;
}

static reduct_rvsdg_origin_t* reduct_build_generic_block(reduct_builder_t* builder, reduct_handle_t list,
    uint32_t startIdx)
{
    reduct_rvsdg_origin_t* last = NULL;

    if (!REDUCT_HANDLE_IS_LIST(list))
    {
        REDUCT_ERROR_COMPILE(builder, list, "expected list, got %s", REDUCT_HANDLE_GET_TYPE_STRING(list));
    }

    reduct_list_t* listPtr = REDUCT_HANDLE_TO_LIST(list);
    for (uint32_t i = startIdx; i < listPtr->length; i++)
    {
        last = reduct_build_handle(builder, listPtr->handles[i]);
    }

    if (last == NULL)
    {
        last = reduct_rvsdg_node_new_simple_constant(builder->reduct, builder->scope->region,
            REDUCT_HANDLE_NIL(builder->reduct))
                   ->output;
    }

    return last;
}

REDUCT_API reduct_handle_t reduct_build(reduct_t* reduct, reduct_handle_t ast)
{
    if (!REDUCT_HANDLE_IS_LIST(ast))
    {
        REDUCT_ERROR_THROW(reduct, "expected list as root of AST, got %s", REDUCT_HANDLE_GET_TYPE_STRING(ast));
    }

    reduct_builder_t builder = {
        .reduct = reduct,
        .lastItem = REDUCT_HANDLE_TO_ITEM(ast),
        .eAtom = reduct_atom_lookup(reduct, "e", 1, REDUCT_ATOM_LOOKUP_NONE),
        .piAtom = reduct_atom_lookup(reduct, "pi", 2, REDUCT_ATOM_LOOKUP_NONE),
        .nilAtom = reduct_atom_lookup(reduct, "nil", 3, REDUCT_ATOM_LOOKUP_NONE),
        .trueAtom = reduct_atom_lookup(reduct, "true", 4, REDUCT_ATOM_LOOKUP_NONE),
        .falseAtom = reduct_atom_lookup(reduct, "false", 5, REDUCT_ATOM_LOOKUP_NONE),
    };

    reduct_rvsdg_node_t* lambda = reduct_rvsdg_node_new_lambda(builder.reduct, NULL);
    reduct_item_t* lambdaItem = REDUCT_CONTAINER_OF(lambda, reduct_item_t, rvsdgNode);
    reduct_item_t* astItem = REDUCT_HANDLE_TO_ITEM(ast);
    lambdaItem->moduleId = astItem->moduleId;
    lambdaItem->modulePos = astItem->modulePos;

    reduct_builder_scope_t scope;
    reduct_builder_enter_scope(&builder, &scope, lambda->firstRegion, lambda);

    reduct_rvsdg_origin_t* origin = reduct_build_handle(&builder, ast);
    reduct_rvsdg_edge_connect(builder.reduct, origin, lambda->firstRegion->result);

    return REDUCT_HANDLE_FROM_RVSDG_NODE(lambda);
}

REDUCT_API void reduct_build_register_intrinsics(struct reduct* reduct)
{
    for (uint32_t i = 0; i < UINT8_MAX + 1; i++)
    {
        if (reductIntrinsics[i].name != NULL)
        {
            reduct_native_register(reduct, (reduct_native_t*)&reductIntrinsics[i], 1);
        }
    }
}

REDUCT_API reduct_native_fn reduct_builder_get_opcode_native(reduct_opcode_t op)
{
    reduct_opcode_t base = REDUCT_OPCODE_BASE(op);
    if (base >= UINT8_MAX + 1)
    {
        return NULL;
    }
    return reductIntrinsics[base].nativeFn;
}
