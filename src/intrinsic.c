#include "reduct/compile.h"
#include "reduct/intrinsic.h"
#include "reduct/item.h"
#include "reduct/standard.h"
#include <reduct/atom.h>
#include <reduct/defs.h>

static inline void reduct_intrinsic_check_arity(reduct_compiler_t* compiler, reduct_item_t* list,
    reduct_size_t expected, const char* name)
{
    if (REDUCT_UNLIKELY(list->length != (reduct_uint32_t)expected + 1))
    {
        REDUCT_ERROR_COMPILE(compiler, list, "%s expects %zu argument(s), got %zu", name,
            (reduct_size_t)expected, (reduct_size_t)list->length - 1);
    }
}

static inline void reduct_intrinsic_check_min_arity(reduct_compiler_t* compiler, reduct_item_t* list, reduct_size_t min,
    const char* name)
{
    if (REDUCT_UNLIKELY(list->length < (reduct_uint32_t)min + 1))
    {
        REDUCT_ERROR_COMPILE(compiler, list, "%s expects at least %zu argument(s), got %zu", name, (reduct_size_t)min,
            (reduct_size_t)list->length - 1);
    }
}

static inline void reduct_intrinsic_check_arity_range(reduct_compiler_t* compiler, reduct_item_t* list,
    reduct_size_t min, reduct_size_t max, const char* name)
{
    if (REDUCT_UNLIKELY(list->length < (reduct_uint32_t)min + 1 || list->length > (reduct_uint32_t)max + 1))
    {
        REDUCT_ERROR_COMPILE(compiler, list, "%s expects %zu to %zu argument(s), got %zu", name,
            (reduct_size_t)min, (reduct_size_t)max, (reduct_size_t)list->length - 1);
    }
}

void reduct_intrinsic_quote(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_arity(compiler, list, 1, "quote");
    *out = REDUCT_EXPR_CONST_ITEM(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1));
}

void reduct_intrinsic_list(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    return reduct_intrinsic_list_generic(compiler, list, 1, out);
}

static inline void reduct_compile_build_into_target(reduct_compiler_t* compiler, reduct_item_t* item,
    reduct_reg_t target)
{
    reduct_expr_t expr = REDUCT_EXPR_TARGET(target);
    reduct_expr_build(compiler, item, &expr);
    if (expr.mode == REDUCT_MODE_NONE)
    {
        reduct_expr_t nil = REDUCT_EXPR_NIL(compiler);
        reduct_compile_move(compiler, target, &nil);
    }
    else if (expr.mode != REDUCT_MODE_REG || expr.reg != target)
    {
        reduct_compile_move(compiler, target, &expr);
        reduct_expr_done(compiler, &expr);
    }
}

void reduct_intrinsic_do(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_block_generic(compiler, list, 1, out);
}

void reduct_intrinsic_lambda(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_min_arity(compiler, list, 2, "lambda");

    reduct_item_t* args = reduct_list_nth_item(compiler->reduct, &list->list, 1);
    if (args->type != REDUCT_ITEM_TYPE_LIST)
    {
        REDUCT_ERROR_COMPILE(compiler, args, "lambda: parameter list must be a list, got %s",
            reduct_item_type_str(args));
    }

    if (args->length > 255)
    {
        REDUCT_ERROR_COMPILE(compiler, args, "lambda: at most 255 parameters allowed, got %d", args->length);
    }

    reduct_function_t* func = reduct_function_new(compiler->reduct);
    reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);
    funcItem->inputId = list->inputId;
    funcItem->position = list->position;
    reduct_const_slot_t slot = REDUCT_CONST_SLOT_ITEM(funcItem);
    reduct_const_t funcConst = reduct_function_lookup_constant(compiler->reduct, compiler->function, &slot);

    func->arity = (reduct_uint8_t)args->length;

    reduct_compiler_t childCompiler;
    reduct_compiler_init(&childCompiler, compiler->reduct, func, compiler);

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH(&h, &args->list)
    {
        reduct_item_t* argItem = REDUCT_HANDLE_TO_ITEM(&h);
        if (argItem->type != REDUCT_ITEM_TYPE_ATOM)
        {
            REDUCT_ERROR_COMPILE(compiler, argItem, "lambda: each parameter must be an atom name, got %s",
                reduct_item_type_str(argItem));
        }

        reduct_atom_t* atom = &argItem->atom;
        if (atom->length > 0 && atom->string[0] == '*')
        {
            if (_iter.index != args->length - 1)
            {
                REDUCT_ERROR_COMPILE(compiler, argItem, "lambda: rest parameter must be the last parameter");
            }

            func->flags |= REDUCT_FUNCTION_FLAG_VARIADIC;

            reduct_atom_t* cleanName = reduct_atom_lookup(compiler->reduct, atom->string + 1, atom->length - 1, REDUCT_ATOM_LOOKUP_NONE);
            reduct_local_add_arg(&childCompiler, cleanName);
        }
        else
        {
            reduct_local_add_arg(&childCompiler, atom);
        }
    }

    reduct_expr_t bodyExpr = REDUCT_EXPR_NONE();
    reduct_intrinsic_block_generic(&childCompiler, list, 2, &bodyExpr);
    reduct_compile_return(&childCompiler, &bodyExpr);
    reduct_expr_done(&childCompiler, &bodyExpr);

    reduct_compiler_deinit(&childCompiler);

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    reduct_compile_closure(compiler, target, funcConst);

    for (reduct_uint32_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == REDUCT_CONST_SLOT_ITEM)
        {
            continue;
        }
        reduct_atom_t* captureName = func->constants[i].capture;
        reduct_local_t* captured = reduct_local_lookup(compiler, func->constants[i].capture);
        if (captured == REDUCT_NULL)
        {
            REDUCT_ERROR_COMPILE(compiler, REDUCT_CONTAINER_OF(captureName, reduct_item_t, atom),
                "undefined local '%.*s'%s", captureName->length, captureName->string, captureName->flags & REDUCT_ATOM_FLAG_OVERFLOW ? " (integer overflow)" : "");
        }

        if (!REDUCT_LOCAL_IS_DEFINED(captured))
        {
            reduct_expr_t selfExpr = REDUCT_EXPR_REG(target);
            reduct_compile_capture(compiler, target, i, &selfExpr);
            continue;
        }

        reduct_compile_capture(compiler, target, i, &captured->expr);
    }

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_thread(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_min_arity(compiler, list, 1, "->");

    reduct_expr_t current = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &current);

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, 2)
    {
        reduct_item_t* step = REDUCT_HANDLE_TO_ITEM(&h);
        reduct_item_t* head;
        reduct_uint32_t arity;

        if (step->type == REDUCT_ITEM_TYPE_LIST)
        {
            if (step->length == 0)
            {
                continue;
            }
            head = reduct_list_nth_item(compiler->reduct, &step->list, 0);
            arity = step->length;
        }
        else
        {
            head = step;
            arity = 1;
        }

        reduct_reg_t base = reduct_reg_alloc_range(compiler, arity);

        reduct_compile_move(compiler, base, &current);
        reduct_expr_done(compiler, &current);

        if (step->type == REDUCT_ITEM_TYPE_LIST)
        {
            reduct_handle_t argH;
            REDUCT_LIST_FOR_EACH_AT(&argH, &step->list, 1)
            {
                reduct_reg_t argReg = (reduct_reg_t)(base + _iter.index);
                reduct_expr_t argExpr = REDUCT_EXPR_TARGET(argReg);
                reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&argH), &argExpr);

                if (argExpr.mode != REDUCT_MODE_REG || argExpr.reg != argReg)
                {
                    reduct_compile_move(compiler, argReg, &argExpr);
                    reduct_expr_done(compiler, &argExpr);
                }
            }
        }

        reduct_expr_t callable = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, head, &callable);
        reduct_compile_call(compiler, base, &callable, arity);
        reduct_expr_done(compiler, &callable);

        if (arity > 1)
        {
            reduct_reg_free_range(compiler, (reduct_reg_t)(base + 1), arity - 1);
        }

        current = REDUCT_EXPR_REG(base);
    }

    *out = current;
}

void reduct_intrinsic_def(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_arity(compiler, list, 2, "def");

    reduct_item_t* name = reduct_list_nth_item(compiler->reduct, &list->list, 1);
    if (name->type != REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_COMPILE(compiler, name, "def: name must be an atom, got %s",
            reduct_item_type_str(name));
    }

    reduct_local_t* local = reduct_local_def(compiler, &name->atom);

    reduct_expr_t valExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 2), &valExpr);

    reduct_local_def_done(compiler, local, &valExpr);
    reduct_expr_done(compiler, &valExpr);

    *out = valExpr;
}

static inline reduct_bool_t reduct_expr_get_item(reduct_compiler_t* compiler, reduct_expr_t* expr,
    reduct_handle_t* outItem)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(expr != REDUCT_NULL);
    REDUCT_ASSERT(outItem != REDUCT_NULL);

    if (expr->mode == REDUCT_MODE_CONST)
    {
        if (compiler->function->constants[expr->constant].type != REDUCT_CONST_SLOT_ITEM)
        {
            return REDUCT_FALSE;
        }

        *outItem = REDUCT_HANDLE_FROM_ITEM(compiler->function->constants[expr->constant].item);
        return REDUCT_TRUE;
    }
    return REDUCT_FALSE;
}

static inline reduct_bool_t reduct_expr_is_known_truthy(reduct_compiler_t* compiler, reduct_expr_t* expr,
    reduct_bool_t* isTruthy)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(expr != REDUCT_NULL);
    REDUCT_ASSERT(isTruthy != REDUCT_NULL);

    reduct_handle_t item;
    if (reduct_expr_get_item(compiler, expr, &item))
    {
        *isTruthy = REDUCT_HANDLE_IS_TRUTHY(&item);
        return REDUCT_TRUE;
    }
    return REDUCT_FALSE;
}

static reduct_item_t* reduct_intrinsic_get_pair(reduct_compiler_t* compiler, reduct_handle_t* h, const char* name)
{
    reduct_item_t* pair = REDUCT_HANDLE_TO_ITEM(h);
    if (pair->type != REDUCT_ITEM_TYPE_LIST || pair->length != 2)
    {
        REDUCT_ERROR_COMPILE(compiler, pair, "%s: each clause must be a list of 2 items, got %s (length %u)", name,
            reduct_item_type_str(pair), pair->length);
    }
    return pair;
}

static reduct_bool_t reduct_fold_comparison(reduct_t* reduct, reduct_opcode_t opBase, reduct_handle_t left,
    reduct_handle_t right, reduct_bool_t* result)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(result != REDUCT_NULL);

    reduct_int64_t cmp = reduct_handle_compare(reduct, &left, &right);
    switch (opBase)
    {
    case REDUCT_OPCODE_EQ:
        *result = (cmp == 0);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_NEQ:
        *result = (cmp != 0);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_SEQ:
        *result = reduct_handle_is_equal(reduct, &left, &right);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_SNEQ:
        *result = !reduct_handle_is_equal(reduct, &left, &right);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_LT:
        *result = (cmp < 0);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_LE:
        *result = (cmp <= 0);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_GT:
        *result = (cmp > 0);
        return REDUCT_TRUE;
    case REDUCT_OPCODE_GE:
        *result = (cmp >= 0);
        return REDUCT_TRUE;
    default:
        return REDUCT_FALSE;
    }
}

void reduct_intrinsic_if(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_arity_range(compiler, list, 2, 3, "if");

    reduct_expr_t condExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &condExpr);

    reduct_bool_t isTruthy;
    if (reduct_expr_is_known_truthy(compiler, &condExpr, &isTruthy))
    {
        reduct_expr_done(compiler, &condExpr);
        if (isTruthy)
        {
            reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 2), out);
        }
        else if (list->length == 4)
        {
            reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 3), out);
        }
        else
        {
            *out = REDUCT_EXPR_NIL(compiler);
        }
        return;
    }

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);

    reduct_reg_t condReg = reduct_compile_move_or_alloc(compiler, &condExpr);
    reduct_size_t jumpElse = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, condReg);
    reduct_expr_done(compiler, &condExpr);

    reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 2), target);

    reduct_size_t jumpEnd = 0;
    if (list->length == 4)
    {
        jumpEnd = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
    }

    reduct_compile_jump_patch(compiler, jumpElse);

    if (list->length == 4)
    {
        reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 3), target);
        reduct_compile_jump_patch(compiler, jumpEnd);
    }
    else
    {
        reduct_expr_t nilExpr = REDUCT_EXPR_NIL(compiler);
        reduct_compile_move(compiler, target, &nilExpr);
    }

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_cond(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (list->length < 2)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_reg_t target = REDUCT_REG_INVALID;
    reduct_size_t jumpsEnd[REDUCT_REGISTER_MAX];
    reduct_size_t jumpCount = 0;
    reduct_bool_t alwaysHit = REDUCT_FALSE;

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, 1)
    {
        reduct_item_t* pair = reduct_intrinsic_get_pair(compiler, &h, "cond");

        reduct_expr_t condExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 0), &condExpr);

        reduct_bool_t isTruthy;
        if (reduct_expr_is_known_truthy(compiler, &condExpr, &isTruthy))
        {
            reduct_expr_done(compiler, &condExpr);
            if (!isTruthy)
            {
                continue;
            }

            if (target == REDUCT_REG_INVALID)
            {
                if (targetHint != REDUCT_REG_INVALID)
                {
                    *out = REDUCT_EXPR_TARGET(targetHint);
                }
                else
                {
                    *out = REDUCT_EXPR_NIL(compiler);
                }
                reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 1), out);
                return;
            }

            reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 1), target);
            alwaysHit = REDUCT_TRUE;
            break;
        }

        if (target == REDUCT_REG_INVALID)
        {
            target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
        }

        reduct_reg_t condReg = reduct_compile_move_or_alloc(compiler, &condExpr);
        reduct_size_t jumpNext = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, condReg);
        reduct_expr_done(compiler, &condExpr);

        reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 1), target);

        if (jumpCount >= REDUCT_REGISTER_MAX)
        {
            REDUCT_ERROR_COMPILE(compiler, list, "cond: too many clauses, limit is %u", REDUCT_REGISTER_MAX);
        }
        jumpsEnd[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);

        reduct_compile_jump_patch(compiler, jumpNext);
    }

    if (target == REDUCT_REG_INVALID)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    if (!alwaysHit)
    {
        reduct_expr_t nilConst = REDUCT_EXPR_NIL(compiler);
        reduct_compile_move(compiler, target, &nilConst);
    }

    reduct_compile_jump_patch_list(compiler, jumpsEnd, jumpCount);

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_match(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_min_arity(compiler, list, 2, "match");

    reduct_expr_t targetExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &targetExpr);

    reduct_handle_t targetItem;
    reduct_bool_t targetKnown = reduct_expr_get_item(compiler, &targetExpr, &targetItem);
    reduct_reg_t targetReg = REDUCT_REG_INVALID;
    reduct_reg_t resultReg = reduct_expr_get_reg(compiler, out);

    reduct_size_t jumpsEnd[REDUCT_REGISTER_MAX];
    reduct_size_t jumpCount = 0;

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, 2)
    {
        if (_iter.index >= list->length - 1)
        {
            break;
        }

        reduct_item_t* pair = reduct_intrinsic_get_pair(compiler, &h, "match");
        reduct_expr_t valExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 0), &valExpr);

        reduct_handle_t valItem;
        if (targetKnown && reduct_expr_get_item(compiler, &valExpr, &valItem))
        {
            reduct_bool_t cmpResult = REDUCT_FALSE;
            if (reduct_fold_comparison(compiler->reduct, REDUCT_OPCODE_EQ, targetItem, valItem, &cmpResult))
            {
                reduct_expr_done(compiler, &valExpr);
                if (!cmpResult)
                {
                    continue;
                }

                reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 1),
                    resultReg);
                reduct_expr_done(compiler, &targetExpr);
                *out = REDUCT_EXPR_REG(resultReg);
                return;
            }
        }

        if (targetReg == REDUCT_REG_INVALID)
        {
            targetReg = reduct_compile_move_or_alloc(compiler, &targetExpr);
        }

        reduct_reg_t cmpResultReg = reduct_reg_alloc(compiler);
        reduct_compile_binary(compiler, REDUCT_OPCODE_EQ, cmpResultReg, targetReg, &valExpr);
        reduct_expr_done(compiler, &valExpr);

        reduct_size_t jumpNext = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, cmpResultReg);
        reduct_reg_free(compiler, cmpResultReg);

        reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &pair->list, 1), resultReg);
        if (REDUCT_UNLIKELY(jumpCount >= REDUCT_REGISTER_MAX))
        {
            REDUCT_ERROR_COMPILE(compiler, list, "match: too many clauses, limit is %u", REDUCT_REGISTER_MAX);
        }
        jumpsEnd[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
        reduct_compile_jump_patch(compiler, jumpNext);
    }

    reduct_compile_build_into_target(compiler, reduct_list_nth_item(compiler->reduct, &list->list, list->length - 1),
        resultReg);

    reduct_compile_jump_patch_list(compiler, jumpsEnd, jumpCount);

    reduct_expr_done(compiler, &targetExpr);
    *out = REDUCT_EXPR_REG(resultReg);
}

static void reduct_intrinsic_and_or(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out,
    reduct_opcode_t jumpOp)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (list->length < 2)
    {
        *out = REDUCT_EXPR_FALSE(compiler);
        return;
    }

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_reg_t target = REDUCT_REG_INVALID;
    reduct_size_t jumps[REDUCT_REGISTER_MAX];
    reduct_size_t jumpCount = 0;

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, 1)
    {
        reduct_expr_t argExpr = REDUCT_EXPR_NONE();
        if (target != REDUCT_REG_INVALID)
        {
            argExpr = REDUCT_EXPR_TARGET(target);
        }

        reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&h), &argExpr);

        reduct_bool_t isTruthy;
        if (reduct_expr_is_known_truthy(compiler, &argExpr, &isTruthy))
        {
            reduct_bool_t shortCircuits = (jumpOp == REDUCT_OPCODE_JMPT) ? isTruthy : !isTruthy;

            if (shortCircuits || _iter.index + 1 >= list->length)
            {
                if (target == REDUCT_REG_INVALID)
                {
                    *out = argExpr;
                    return;
                }

                reduct_compile_move(compiler, target, &argExpr);
                reduct_expr_done(compiler, &argExpr);
                break;
            }

            reduct_expr_done(compiler, &argExpr);
            continue;
        }

        if (target == REDUCT_REG_INVALID)
        {
            target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
        }

        if (argExpr.mode != REDUCT_MODE_REG || argExpr.reg != target)
        {
            reduct_compile_move(compiler, target, &argExpr);
            reduct_expr_done(compiler, &argExpr);
        }

        if (_iter.index + 1 != list->length)
        {
            if (REDUCT_UNLIKELY(jumpCount >= REDUCT_REGISTER_MAX))
            {
                REDUCT_ERROR_COMPILE(compiler, list, "and/or: too many operands, limit is %u",
                    REDUCT_REGISTER_MAX);
            }
            jumps[jumpCount++] = reduct_compile_jump(compiler, jumpOp, target);
        }
    }

    reduct_compile_jump_patch_list(compiler, jumps, jumpCount);

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_and(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_and_or(compiler, list, out, REDUCT_OPCODE_JMPF);
}

void reduct_intrinsic_or(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_and_or(compiler, list, out, REDUCT_OPCODE_JMPT);
}

void reduct_intrinsic_not(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_arity(compiler, list, 1, "not");

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);

    reduct_expr_t argExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &argExpr);

    reduct_bool_t isTruthy;
    if (reduct_expr_is_known_truthy(compiler, &argExpr, &isTruthy))
    {
        reduct_expr_done(compiler, &argExpr);
        *out = isTruthy ? REDUCT_EXPR_FALSE(compiler) : REDUCT_EXPR_TRUE(compiler);
        return;
    }

    reduct_reg_t argReg = reduct_compile_move_or_alloc(compiler, &argExpr);
    reduct_size_t jumpTrue = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPT, argReg);
    reduct_expr_done(compiler, &argExpr);

    reduct_expr_t trueExpr = REDUCT_EXPR_TRUE(compiler);
    reduct_compile_move(compiler, target, &trueExpr);

    reduct_size_t jumpEnd = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);

    reduct_compile_jump_patch(compiler, jumpTrue);
    reduct_expr_t falseExpr = REDUCT_EXPR_FALSE(compiler);
    reduct_compile_move(compiler, target, &falseExpr);

    reduct_compile_jump_patch(compiler, jumpEnd);

    *out = REDUCT_EXPR_REG(target);
}

static inline reduct_atom_t* reduct_fold_binary_calc(reduct_compiler_t* compiler, reduct_opcode_t op, reduct_float_t lf,
    reduct_float_t rf, reduct_int64_t li, reduct_int64_t ri, reduct_bool_t isFloat)
{
    if (isFloat)
    {
        switch (op)
        {
        case REDUCT_OPCODE_ADD:
            return reduct_atom_new_float(compiler->reduct, lf + rf);
        case REDUCT_OPCODE_SUB:
            return reduct_atom_new_float(compiler->reduct, lf - rf);
        case REDUCT_OPCODE_MUL:
            return reduct_atom_new_float(compiler->reduct, lf * rf);
        case REDUCT_OPCODE_DIV:
            return (rf == 0.0) ? REDUCT_NULL : reduct_atom_new_float(compiler->reduct, lf / rf);
        default:
            return REDUCT_NULL;
        }
    }
    else
    {
        switch (op)
        {
        case REDUCT_OPCODE_ADD:
            return reduct_atom_new_int(compiler->reduct, li + ri);
        case REDUCT_OPCODE_SUB:
            return reduct_atom_new_int(compiler->reduct, li - ri);
        case REDUCT_OPCODE_MUL:
            return reduct_atom_new_int(compiler->reduct, li * ri);
        case REDUCT_OPCODE_DIV:
            return (ri == 0) ? REDUCT_NULL : reduct_atom_new_int(compiler->reduct, li / ri);
        case REDUCT_OPCODE_MOD:
            return (ri == 0) ? REDUCT_NULL : reduct_atom_new_int(compiler->reduct, li % ri);
        case REDUCT_OPCODE_BAND:
            return reduct_atom_new_int(compiler->reduct, li & ri);
        case REDUCT_OPCODE_BOR:
            return reduct_atom_new_int(compiler->reduct, li | ri);
        case REDUCT_OPCODE_BXOR:
            return reduct_atom_new_int(compiler->reduct, li ^ ri);
        case REDUCT_OPCODE_SHL:
            return (ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH) ? REDUCT_NULL : reduct_atom_new_int(compiler->reduct, li << ri);
        case REDUCT_OPCODE_SHR:
            return (ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH) ? REDUCT_NULL : reduct_atom_new_int(compiler->reduct, li >> ri);
        default:
            return REDUCT_NULL;
        }
    }
}

static reduct_bool_t reduct_fold_binary_expr(reduct_compiler_t* compiler, reduct_opcode_t opBase,
    reduct_expr_t* leftExpr, reduct_expr_t* rightExpr, reduct_expr_t* outExpr)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(leftExpr != REDUCT_NULL);
    REDUCT_ASSERT(rightExpr != REDUCT_NULL);

    REDUCT_ASSERT(outExpr != REDUCT_NULL);
    if (leftExpr->mode != REDUCT_MODE_CONST || rightExpr->mode != REDUCT_MODE_CONST)
    {
        return REDUCT_FALSE;
    }

    if (compiler->function->constants[leftExpr->constant].type != REDUCT_CONST_SLOT_ITEM ||
        compiler->function->constants[rightExpr->constant].type != REDUCT_CONST_SLOT_ITEM)
    {
        return REDUCT_FALSE;
    }

    reduct_item_t* leftItem = compiler->function->constants[leftExpr->constant].item;
    reduct_item_t* rightItem = compiler->function->constants[rightExpr->constant].item;

    if (leftItem->type != REDUCT_ITEM_TYPE_ATOM || rightItem->type != REDUCT_ITEM_TYPE_ATOM)
    {
        return REDUCT_FALSE;
    }

    if (!reduct_atom_is_number(&leftItem->atom) || !reduct_atom_is_number(&rightItem->atom))
    {
        return REDUCT_FALSE;
    }

    reduct_bool_t isFloat = reduct_atom_is_float(&leftItem->atom) || reduct_atom_is_float(&rightItem->atom);

    reduct_float_t lf = reduct_atom_get_float(&leftItem->atom);
    reduct_float_t rf = reduct_atom_get_float(&rightItem->atom);
    reduct_int64_t li = reduct_atom_get_int(&leftItem->atom);
    reduct_int64_t ri = reduct_atom_get_int(&rightItem->atom);

    reduct_atom_t* result = reduct_fold_binary_calc(compiler, opBase, lf, rf, li, ri, isFloat);
    if (result == REDUCT_NULL)
    {
        return REDUCT_FALSE;
    }

    *outExpr = REDUCT_EXPR_CONST_ATOM(compiler, result);
    return REDUCT_TRUE;
}

void reduct_intrinsic_binary_generic(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out,
    reduct_opcode_t opBase)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (opBase == REDUCT_OPCODE_MOD || opBase == REDUCT_OPCODE_SHL || opBase == REDUCT_OPCODE_SHR)
    {
        reduct_intrinsic_check_arity(compiler, list, 2, "operator");
    }
    else if (opBase >= REDUCT_OPCODE_BAND && opBase <= REDUCT_OPCODE_BXOR)
    {
        reduct_intrinsic_check_min_arity(compiler, list, 2, "bitwise operator");
    }
    else
    {
        reduct_intrinsic_check_min_arity(compiler, list, 1, "arithmetic operator");
    }

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &leftExpr);

    if (list->length == 2)
    {
        if (opBase == REDUCT_OPCODE_SUB || opBase == REDUCT_OPCODE_DIV)
        {
            reduct_expr_t initialExpr =
                (opBase == REDUCT_OPCODE_SUB) ? REDUCT_EXPR_INT(compiler, 0) : REDUCT_EXPR_INT(compiler, 1);
            reduct_expr_t foldedExpr;
            if (reduct_fold_binary_expr(compiler, opBase, &initialExpr, &leftExpr, &foldedExpr))
            {
                reduct_expr_done(compiler, &leftExpr);
                *out = foldedExpr;
                return;
            }

            reduct_reg_t initialReg = reduct_compile_move_or_alloc(compiler, &initialExpr);
            reduct_reg_t target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
            reduct_compile_binary(compiler, opBase, target, initialReg, &leftExpr);

            reduct_expr_done(compiler, &leftExpr);
            reduct_expr_done(compiler, &initialExpr);
            *out = REDUCT_EXPR_REG(target);
            return;
        }

        *out = leftExpr;
        return;
    }

    reduct_bool_t hasAccumulator = REDUCT_FALSE;
    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(&list->list, 2);
    reduct_handle_t h;
    while (reduct_list_iter_next(&iter, &h))
    {
        reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&h), &rightExpr);

        reduct_expr_t foldedExpr;
        if (reduct_fold_binary_expr(compiler, opBase, &leftExpr, &rightExpr, &foldedExpr))
        {
            reduct_expr_done(compiler, &leftExpr);
            reduct_expr_done(compiler, &rightExpr);
            leftExpr = foldedExpr;
            continue;
        }

        if (!hasAccumulator)
        {
            if (leftExpr.mode != REDUCT_MODE_REG)
            {
                reduct_compile_move_or_alloc(compiler, &leftExpr);
            }

            reduct_reg_t target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
            reduct_compile_binary(compiler, opBase, target, leftExpr.reg, &rightExpr);
            reduct_expr_done(compiler, &leftExpr);
            leftExpr = REDUCT_EXPR_REG(target);
            hasAccumulator = REDUCT_TRUE;
        }
        else
        {
            reduct_compile_binary(compiler, opBase, leftExpr.reg, leftExpr.reg, &rightExpr);
        }

        reduct_expr_done(compiler, &rightExpr);
    }

    *out = leftExpr;
}

void reduct_intrinsic_add(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_ADD);
}

void reduct_intrinsic_sub(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SUB);
}

void reduct_intrinsic_mul(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_MUL);
}

void reduct_intrinsic_div(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_DIV);
}

void reduct_intrinsic_mod(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_MOD);
}

static void reduct_intrinsic_unary_op_generic(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out,
    reduct_opcode_t op, reduct_expr_t rightExpr, const char* name)
{
    reduct_intrinsic_check_arity(compiler, list, 1, name);

    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &leftExpr);

    reduct_expr_t foldedExpr;
    if (reduct_fold_binary_expr(compiler, op, &leftExpr, &rightExpr, &foldedExpr))
    {
        reduct_expr_done(compiler, &leftExpr);
        reduct_expr_done(compiler, &rightExpr);
        *out = foldedExpr;
        return;
    }

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    reduct_compile_binary(compiler, op, target, reduct_compile_move_or_alloc(compiler, &leftExpr), &rightExpr);
    reduct_expr_done(compiler, &leftExpr);
    reduct_expr_done(compiler, &rightExpr);
    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_inc(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_ADD, REDUCT_EXPR_INT(compiler, 1), "inc");
}

void reduct_intrinsic_dec(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_SUB, REDUCT_EXPR_INT(compiler, 1), "dec");
}

void reduct_intrinsic_bit_and(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BAND);
}

void reduct_intrinsic_bit_or(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BOR);
}

void reduct_intrinsic_bit_xor(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BXOR);
}

void reduct_intrinsic_bit_not(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_arity(compiler, list, 1, "bitwise not");

    reduct_expr_t argExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &argExpr);

    if (argExpr.mode == REDUCT_MODE_CONST &&
        compiler->function->constants[argExpr.constant].type == REDUCT_CONST_SLOT_ITEM)
    {
        reduct_item_t* argItem = compiler->function->constants[argExpr.constant].item;
        if (argItem->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_int(&argItem->atom))
        {
            reduct_atom_t* result = reduct_atom_new_int(compiler->reduct, ~reduct_atom_get_int(&argItem->atom));
            reduct_expr_done(compiler, &argExpr);
            *out = REDUCT_EXPR_CONST_ATOM(compiler, result);
            return;
        }
    }

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_BNOT | argExpr.mode), target, 0, argExpr.value));
    reduct_expr_done(compiler, &argExpr);
    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_bit_shl(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SHL);
}

void reduct_intrinsic_bit_shr(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SHR);
}

static void reduct_intrinsic_comparison_generic(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out,
    reduct_opcode_t opBase)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_intrinsic_check_min_arity(compiler, list, 2, "comparison");

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, reduct_list_nth_item(compiler->reduct, &list->list, 1), &leftExpr);

    reduct_reg_t target = REDUCT_REG_INVALID;
    reduct_size_t jumps[REDUCT_REGISTER_MAX];
    reduct_size_t jumpCount = 0;

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, 2)
    {
        reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&h), &rightExpr);

        reduct_handle_t leftItem, rightItem;
        if (reduct_expr_get_item(compiler, &leftExpr, &leftItem) &&
            reduct_expr_get_item(compiler, &rightExpr, &rightItem))
        {
            reduct_bool_t cmpResult = REDUCT_FALSE;
            if (reduct_fold_comparison(compiler->reduct, opBase, leftItem, rightItem, &cmpResult))
            {
                if (cmpResult)
                {
                    reduct_expr_done(compiler, &leftExpr);
                    leftExpr = rightExpr;
                    continue;
                }
                else
                {
                    reduct_expr_done(compiler, &leftExpr);
                    reduct_expr_done(compiler, &rightExpr);

                    if (jumpCount > 0)
                    {
                        reduct_expr_t falseExpr = REDUCT_EXPR_FALSE(compiler);
                        reduct_compile_move(compiler, target, &falseExpr);
                        reduct_compile_jump_patch_list(compiler, jumps, jumpCount);
                        *out = REDUCT_EXPR_REG(target);
                    }
                    else
                    {
                        if (target != REDUCT_REG_INVALID)
                        {
                            reduct_reg_free(compiler, target);
                        }
                        *out = REDUCT_EXPR_FALSE(compiler);
                    }
                    return;
                }
            }
        }

        if (target == REDUCT_REG_INVALID)
        {
            target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
        }

        if (leftExpr.mode != REDUCT_MODE_REG)
        {
            reduct_compile_move_or_alloc(compiler, &leftExpr);
        }

        reduct_compile_binary(compiler, opBase, target, leftExpr.reg, &rightExpr);

        if (_iter.index + 1 != list->length)
        {
            if (jumpCount >= REDUCT_REGISTER_MAX)
            {
                REDUCT_ERROR_COMPILE(compiler, list, "comparison: too many operands, limit is %u", REDUCT_REGISTER_MAX);
            }
            jumps[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, target);

            reduct_expr_done(compiler, &leftExpr);
            leftExpr = rightExpr;
        }
        else
        {
            reduct_expr_done(compiler, &leftExpr);
            reduct_expr_done(compiler, &rightExpr);
            leftExpr = REDUCT_EXPR_NONE();
        }
    }

    reduct_expr_done(compiler, &leftExpr);

    if (jumpCount > 0)
    {
        reduct_compile_jump_patch_list(compiler, jumps, jumpCount);
        *out = REDUCT_EXPR_REG(target);
    }
    else if (target == REDUCT_REG_INVALID)
    {
        *out = REDUCT_EXPR_TRUE(compiler);
    }
    else
    {
        *out = REDUCT_EXPR_REG(target);
    }
}

void reduct_intrinsic_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_EQ);
}

void reduct_intrinsic_exact_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_SEQ);
}

void reduct_intrinsic_not_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_NEQ);
}

void reduct_intrinsic_exact_not_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_SNEQ);
}

void reduct_intrinsic_less(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_LT);
}

void reduct_intrinsic_less_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_LE);
}

void reduct_intrinsic_greater(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_GT);
}

void reduct_intrinsic_greater_equal(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_GE);
}

#define REDUCT_INTRINSIC_NATIVE_ARITH(_name, _op, _identity) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, reduct_size_t argc, \
        reduct_handle_t* argv) \
    { \
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_FROM_INT(_identity); \
        } \
        if (argc == 1) \
        { \
            reduct_handle_t res; \
            reduct_handle_t id = REDUCT_HANDLE_FROM_INT(_identity); \
            REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &id, &argv[0], _op); \
            return res; \
        } \
        reduct_handle_t res = argv[0]; \
        for (reduct_size_t i = 1; i < argc; i++) \
        { \
            REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &res, &argv[i], _op); \
        } \
        return res; \
    }

#define REDUCT_INTRINSIC_NATIVE_LOGIC(_name, _short_circuit_truth) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, reduct_size_t argc, \
        reduct_handle_t* argv) \
    { \
        REDUCT_UNUSED(reduct); \
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_FALSE(); \
        } \
        reduct_handle_t res = argv[0]; \
        for (reduct_size_t i = 0; i < argc; i++) \
        { \
            res = argv[i]; \
            if (REDUCT_HANDLE_IS_TRUTHY(&res) == (_short_circuit_truth)) \
            { \
                return res; \
            } \
        } \
        return res; \
    }

#define REDUCT_INTRINSIC_NATIVE_BITWISE(_name, _op) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, reduct_size_t argc, \
        reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, #_op ": expected at least 2 argument(s), got %zu", (reduct_size_t)argc); \
        reduct_int64_t res = reduct_get_int(reduct, &argv[0]); \
        for (reduct_size_t i = 1; i < argc; i++) \
        { \
            res _op## = reduct_get_int(reduct, &argv[i]); \
        } \
        return REDUCT_HANDLE_FROM_INT(res); \
    }

#define REDUCT_INTRINSIC_NATIVE_COMPARE(_name, _op) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, reduct_size_t argc, \
        reduct_handle_t* argv) \
    { \
        if (argc < 2) \
        { \
            return REDUCT_HANDLE_TRUE(); \
        } \
        for (reduct_size_t i = 0; i < argc - 1; i++) \
        { \
            if (!(reduct_handle_compare(reduct, &argv[i], &argv[i + 1]) _op 0)) \
            { \
                return REDUCT_HANDLE_FALSE(); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    }

#define REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(_name, _expected) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, reduct_size_t argc, \
        reduct_handle_t* argv) \
    { \
        if (argc < 2) \
        { \
            return REDUCT_HANDLE_TRUE(); \
        } \
        for (reduct_size_t i = 0; i < argc - 1; i++) \
        { \
            if (reduct_handle_is_equal(reduct, &argv[i], &argv[i + 1]) != (_expected)) \
            { \
                return REDUCT_HANDLE_FALSE(); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    }

REDUCT_INTRINSIC_NATIVE_ARITH(add, +, 0)
REDUCT_INTRINSIC_NATIVE_ARITH(mul, *, 1)
REDUCT_INTRINSIC_NATIVE_ARITH(sub, -, 0)

REDUCT_INTRINSIC_NATIVE_BITWISE(band, &)
REDUCT_INTRINSIC_NATIVE_BITWISE(bor, |)
REDUCT_INTRINSIC_NATIVE_BITWISE(bxor, ^)

REDUCT_INTRINSIC_NATIVE_COMPARE(eq, ==)
REDUCT_INTRINSIC_NATIVE_COMPARE(neq, !=)
REDUCT_INTRINSIC_NATIVE_COMPARE(lt, <)
REDUCT_INTRINSIC_NATIVE_COMPARE(le, <=)
REDUCT_INTRINSIC_NATIVE_COMPARE(gt, >)
REDUCT_INTRINSIC_NATIVE_COMPARE(ge, >=)

REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(seq, REDUCT_TRUE)
REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(sneq, REDUCT_FALSE)

REDUCT_INTRINSIC_NATIVE_LOGIC(and, REDUCT_FALSE)
REDUCT_INTRINSIC_NATIVE_LOGIC(or, REDUCT_TRUE)

static reduct_handle_t reduct_intrinsic_native_list(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    reduct_list_t* list = reduct_list_new(reduct);
    for (reduct_size_t i = 0; i < argc; i++)
    {
        reduct_list_push(reduct, list, argv[i]);
    }
    return REDUCT_HANDLE_FROM_LIST(list);
}

static reduct_handle_t reduct_intrinsic_native_div(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1, "/: expected at least 1 argument(s), got %zu", (reduct_size_t)argc);
    if (argc == 1)
    {
        reduct_handle_t res;
        reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
        REDUCT_HANDLE_DIV_FAST(reduct, &res, &one, &argv[0]);
        return res;
    }
    reduct_handle_t res = argv[0];
    for (reduct_size_t i = 1; i < argc; i++)
    {
        REDUCT_HANDLE_DIV_FAST(reduct, &res, &res, &argv[i]);
    }
    return res;
}

static reduct_handle_t reduct_intrinsic_native_mod(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "%%: expected 2 argument(s), got %zu", (reduct_size_t)argc);
    reduct_handle_t result;
    REDUCT_HANDLE_MOD_FAST(reduct, &result, &argv[0], &argv[1]);
    return result;
}

static reduct_handle_t reduct_intrinsic_native_inc(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "++: expected 1 argument(s), got %zu", (reduct_size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &argv[0], &one, +);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_dec(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "--: expected 1 argument(s), got %zu", (reduct_size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &argv[0], &one, -);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_bnot(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "~: expected 1 argument(s), got %zu", (reduct_size_t)argc);
    return REDUCT_HANDLE_FROM_INT(~reduct_get_int(reduct, &argv[0]));
}

static reduct_handle_t reduct_intrinsic_native_shl(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "<<: expected 2 argument(s), got %zu", (reduct_size_t)argc);
    reduct_int64_t left = reduct_get_int(reduct, &argv[0]);
    reduct_int64_t right = reduct_get_int(reduct, &argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_INT_WIDTH)
    {
        REDUCT_ERROR_RUNTIME(reduct, "<<: shift amount must be 0-%lu, got %lld", (unsigned long)REDUCT_HANDLE_INT_WIDTH, (long long)right);
    }
    return REDUCT_HANDLE_FROM_INT(left << right);
}

static reduct_handle_t reduct_intrinsic_native_shr(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, ">>: expected 2 argument(s), got %zu", (reduct_size_t)argc);
    reduct_int64_t left = reduct_get_int(reduct, &argv[0]);
    reduct_int64_t right = reduct_get_int(reduct, &argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_INT_WIDTH)
    {
        REDUCT_ERROR_RUNTIME(reduct, ">>: shift amount must be 0-%lu, got %lld", (unsigned long)REDUCT_HANDLE_INT_WIDTH - 1, (long long)right);
    }
    return REDUCT_HANDLE_FROM_INT(left >> right);
}

static reduct_handle_t reduct_intrinsic_native_do(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    if (argc == 0)
    {
        return reduct_handle_nil(reduct);
    }
    return argv[argc - 1];
}

static reduct_handle_t reduct_intrinsic_native_not(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "not: expected 1 argument(s), got %zu", (reduct_size_t)argc);
    return REDUCT_HANDLE_IS_TRUTHY(&argv[0]) ? REDUCT_HANDLE_FALSE() : REDUCT_HANDLE_TRUE();
}

static reduct_native_t reductIntrinsics[] = {
    {"quote", REDUCT_NULL, reduct_intrinsic_quote},
    {"list", reduct_intrinsic_native_list, reduct_intrinsic_list},
    {"do", reduct_intrinsic_native_do, reduct_intrinsic_do},
    {"lambda", REDUCT_NULL, reduct_intrinsic_lambda},
    {"->", REDUCT_NULL, reduct_intrinsic_thread},
    {"def", REDUCT_NULL, reduct_intrinsic_def},
    {"if", REDUCT_NULL, reduct_intrinsic_if},
    {"cond", REDUCT_NULL, reduct_intrinsic_cond},
    {"match", REDUCT_NULL, reduct_intrinsic_match},
    {"and", reduct_intrinsic_native_and, reduct_intrinsic_and},
    {"or", reduct_intrinsic_native_or, reduct_intrinsic_or},
    {"not", reduct_intrinsic_native_not, reduct_intrinsic_not},
    {"+", reduct_intrinsic_native_add, reduct_intrinsic_add},
    {"-", reduct_intrinsic_native_sub, reduct_intrinsic_sub},
    {"*", reduct_intrinsic_native_mul, reduct_intrinsic_mul},
    {"/", reduct_intrinsic_native_div, reduct_intrinsic_div},
    {"%", reduct_intrinsic_native_mod, reduct_intrinsic_mod},
    {"++", reduct_intrinsic_native_inc, reduct_intrinsic_inc},
    {"--", reduct_intrinsic_native_dec, reduct_intrinsic_dec},
    {"&", reduct_intrinsic_native_band, reduct_intrinsic_bit_and},
    {"|", reduct_intrinsic_native_bor, reduct_intrinsic_bit_or},
    {"^", reduct_intrinsic_native_bxor, reduct_intrinsic_bit_xor},
    {"~", reduct_intrinsic_native_bnot, reduct_intrinsic_bit_not},
    {"<<", reduct_intrinsic_native_shl, reduct_intrinsic_bit_shl},
    {">>", reduct_intrinsic_native_shr, reduct_intrinsic_bit_shr},
    {"==", reduct_intrinsic_native_eq, reduct_intrinsic_equal},
    {"!=", reduct_intrinsic_native_neq, reduct_intrinsic_not_equal},
    {"exact==", reduct_intrinsic_native_seq, reduct_intrinsic_exact_equal},
    {"exact!=", reduct_intrinsic_native_sneq, reduct_intrinsic_exact_not_equal},
    {"<", reduct_intrinsic_native_lt, reduct_intrinsic_less},
    {"<=", reduct_intrinsic_native_le, reduct_intrinsic_less_equal},
    {">", reduct_intrinsic_native_gt, reduct_intrinsic_greater},
    {">=", reduct_intrinsic_native_ge, reduct_intrinsic_greater_equal},
};

REDUCT_API void reduct_intrinsic_register_all(reduct_t* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_native_register(reduct, reductIntrinsics, sizeof(reductIntrinsics) / sizeof(reductIntrinsics[0]));
}

REDUCT_API void reduct_intrinsic_block_generic(reduct_compiler_t* compiler, reduct_item_t* list, reduct_uint32_t startIdx,
    reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (startIdx >= list->length)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_uint16_t savedLocalCount = compiler->localCount;

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, startIdx)
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&h);

        if (_iter.index + 1 == list->length)
        {
            if (targetHint != REDUCT_REG_INVALID)
            {
                reduct_compile_build_into_target(compiler, item, targetHint);
                *out = REDUCT_EXPR_REG(targetHint);
            }
            else
            {
                reduct_expr_build(compiler, item, out);
            }
        }
        else
        {
            reduct_expr_t expr = REDUCT_EXPR_NONE();
            reduct_expr_build(compiler, item, &expr);
            reduct_expr_done(compiler, &expr);
        }
    }

    reduct_local_pop(compiler, savedLocalCount, out);
}


REDUCT_API void reduct_intrinsic_list_generic(reduct_compiler_t* compiler, reduct_item_t* list, reduct_uint32_t startIdx, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_uint32_t count = list->length - startIdx;
    if (count == 0)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_reg_t base = reduct_reg_alloc_range(compiler, count);

    reduct_handle_t h;
    REDUCT_LIST_FOR_EACH_AT(&h, &list->list, startIdx)
    {
        reduct_size_t index = _iter.index - startIdx;        
        reduct_expr_t elemExpr = REDUCT_EXPR_TARGET(base + index);
        reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&h), &elemExpr);
        if (elemExpr.mode != REDUCT_MODE_REG || elemExpr.reg != base + index)
        {
            reduct_compile_move(compiler, base + index, &elemExpr);
            reduct_expr_done(compiler, &elemExpr);
        }
    }

    reduct_compile_list(compiler, base, count);
    reduct_reg_free_range(compiler, base + 1, count - 1);

    *out = REDUCT_EXPR_REG(base);
}