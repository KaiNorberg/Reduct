#include "reduct/intrinsic.h"
#include "reduct/compile.h"
#include "reduct/item.h"
#include "reduct/list.h"
#include "reduct/standard.h"
#include <reduct/atom.h>
#include <reduct/defs.h>

void reduct_intrinsic_quote(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 2, "quote: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t arg = reduct_list_nth(compiler->reduct, list, 1);
    *out = REDUCT_EXPR_HANDLE(compiler, &arg);
}

void reduct_intrinsic_list(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    return reduct_intrinsic_list_generic(compiler, list, 1, out);
}

static inline void reduct_compile_build_into_target(reduct_compiler_t* compiler, reduct_handle_t* handle,
    reduct_reg_t target)
{
    reduct_expr_t expr = REDUCT_EXPR_TARGET(target);
    reduct_expr_build(compiler, handle, &expr);
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

void reduct_intrinsic_do(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_block_generic(compiler, list, 1, out);
}

void reduct_intrinsic_lambda(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 2, "lambda: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t args = reduct_list_second(compiler->reduct, list);
    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_LIST(&args), "lambda: parameter list must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(&args));

    reduct_list_t* argsList = REDUCT_HANDLE_TO_LIST(&args);
    REDUCT_ERROR_COMPILE_ASSERT(compiler, argsList->length <= UINT8_MAX,
        "lambda: at most %d parameters allowed, got %d", UINT8_MAX, argsList->length);

    reduct_function_t* func = reduct_function_new(compiler->reduct);

    reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);
    reduct_item_t* listItem = REDUCT_CONTAINER_OF(list, reduct_item_t, list);
    funcItem->inputId = listItem->inputId;
    funcItem->position = listItem->position;

    reduct_const_slot_t slot = REDUCT_CONST_SLOT_FUNCTION(func);
    reduct_const_t funcConst = reduct_function_lookup_constant(compiler->reduct, compiler->function, &slot);

    func->arity = (uint8_t)argsList->length;

    reduct_compiler_t childCompiler;
    reduct_compiler_init(&childCompiler, compiler->reduct, func, compiler);

    reduct_handle_t arg;
    REDUCT_LIST_FOR_EACH(&arg, argsList)
    {
        REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_ATOM(&arg),
            "lambda: the name of each parameter must be an atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(&arg));

        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(&arg);
        if (atom->length > 0 && atom->string[0] == '*')
        {
            REDUCT_ERROR_COMPILE_ASSERT(compiler, _iter.index == argsList->length - 1,
                "lambda: rest parameter must be the last parameter");

            func->flags |= REDUCT_FUNCTION_FLAG_VARIADIC;

            reduct_atom_t* cleanName =
                reduct_atom_lookup(compiler->reduct, atom->string + 1, atom->length - 1, REDUCT_ATOM_LOOKUP_NONE);
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

    for (uint32_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == REDUCT_CONST_SLOT_TYPE_HANDLE)
        {
            continue;
        }

        reduct_atom_t* captureName = func->constants[i].capture;
        reduct_local_t* captured = reduct_local_lookup(compiler, func->constants[i].capture);
        REDUCT_ERROR_COMPILE_ASSERT(compiler, captured != NULL, "undefined local '%.*s'%s", captureName->length,
            captureName->string, captureName->flags & REDUCT_ATOM_FLAG_OVERFLOW ? " (integer overflow)" : "");

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

void reduct_intrinsic_thread(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 2, "->: expected at least 1 argument");

    reduct_handle_t first = reduct_list_second(compiler->reduct, list);

    reduct_expr_t current = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &first, &current);

    reduct_handle_t step;
    REDUCT_LIST_FOR_EACH_AT(&step, list, 2)
    {
        reduct_handle_t head;
        uint32_t arity;

        if (REDUCT_HANDLE_IS_LIST(&step))
        {
            reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(&step);
            if (stepList->length == 0)
            {
                continue;
            }
            head = reduct_list_first(compiler->reduct, stepList);
            arity = stepList->length;
        }
        else
        {
            head = step;
            arity = 1;
        }

        reduct_reg_t base = reduct_reg_alloc_range(compiler, arity);

        reduct_compile_move(compiler, base, &current);
        reduct_expr_done(compiler, &current);

        if (REDUCT_HANDLE_IS_LIST(&step))
        {
            reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(&step);

            reduct_handle_t arg;
            REDUCT_LIST_FOR_EACH_AT(&arg, stepList, 1)
            {
                reduct_reg_t argReg = (reduct_reg_t)(base + _iter.index);
                reduct_expr_t argExpr = REDUCT_EXPR_TARGET(argReg);
                reduct_expr_build(compiler, &arg, &argExpr);

                if (argExpr.mode != REDUCT_MODE_REG || argExpr.reg != argReg)
                {
                    reduct_compile_move(compiler, argReg, &argExpr);
                    reduct_expr_done(compiler, &argExpr);
                }
            }
        }

        reduct_expr_t callable = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, &head, &callable);
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

void reduct_intrinsic_def(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 3, "def: expected 2 arguments, got %u", list->length);

    reduct_handle_t name = reduct_list_nth(compiler->reduct, list, 1);
    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_ATOM(&name), "def: name must be an atom, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(&name));
    reduct_handle_t val = reduct_list_nth(compiler->reduct, list, 2);

    reduct_local_t* local = reduct_local_def(compiler, REDUCT_HANDLE_TO_ATOM(&name));

    reduct_expr_t valExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &val, &valExpr);

    reduct_local_def_done(compiler, local, &valExpr);
    reduct_expr_done(compiler, &valExpr);

    *out = valExpr;
}

static inline bool reduct_expr_get_handle(reduct_compiler_t* compiler, reduct_expr_t* expr, reduct_handle_t* out)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    assert(out != NULL);

    if (expr->mode == REDUCT_MODE_CONST)
    {
        if (compiler->function->constants[expr->constant].type != REDUCT_CONST_SLOT_TYPE_HANDLE)
        {
            return false;
        }

        *out = compiler->function->constants[expr->constant].handle;
        return true;
    }
    return false;
}

static inline bool reduct_expr_is_known_truthy(reduct_compiler_t* compiler, reduct_expr_t* expr, bool* isTruthy)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    assert(isTruthy != NULL);

    reduct_handle_t item;
    if (reduct_expr_get_handle(compiler, expr, &item))
    {
        *isTruthy = REDUCT_HANDLE_IS_TRUTHY(&item);
        return true;
    }
    return false;
}

static reduct_list_t* reduct_intrinsic_get_pair(reduct_compiler_t* compiler, reduct_handle_t* h, const char* name)
{
    assert(compiler != NULL);
    assert(h != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_LIST(h), "%s: each clause must be a list, got %s", name,
        REDUCT_HANDLE_GET_TYPE_STR(h));

    reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(h);
    REDUCT_ERROR_COMPILE_ASSERT(compiler, pair->length == 2, "%s: each clause must be a list of 2 items, got length %u",
        name, pair->length);

    return pair;
}

static bool reduct_fold_comparison(reduct_t* reduct, reduct_opcode_t opBase, reduct_handle_t left,
    reduct_handle_t right, bool* result)
{
    assert(reduct != NULL);
    assert(result != NULL);

    int64_t cmp = reduct_handle_compare(reduct, &left, &right);
    switch (opBase)
    {
    case REDUCT_OPCODE_EQ:
        *result = (cmp == 0);
        return true;
    case REDUCT_OPCODE_NEQ:
        *result = (cmp != 0);
        return true;
    case REDUCT_OPCODE_SEQ:
        *result = reduct_handle_is_equal(reduct, &left, &right);
        return true;
    case REDUCT_OPCODE_SNEQ:
        *result = !reduct_handle_is_equal(reduct, &left, &right);
        return true;
    case REDUCT_OPCODE_LT:
        *result = (cmp < 0);
        return true;
    case REDUCT_OPCODE_LE:
        *result = (cmp <= 0);
        return true;
    case REDUCT_OPCODE_GT:
        *result = (cmp > 0);
        return true;
    case REDUCT_OPCODE_GE:
        *result = (cmp >= 0);
        return true;
    default:
        return false;
    }
}

void reduct_intrinsic_if(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 3 || list->length == 4,
        "if: expected 2 or 3 arguments, got %zu", (size_t)list->length - 1);

    reduct_handle_t cond = reduct_list_nth(compiler->reduct, list, 1);
    reduct_handle_t then = reduct_list_nth(compiler->reduct, list, 2);
    reduct_handle_t els = (list->length == 4) ? reduct_list_nth(compiler->reduct, list, 3) : REDUCT_HANDLE_NONE;

    reduct_expr_t condExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &cond, &condExpr);

    bool isTruthy;
    if (reduct_expr_is_known_truthy(compiler, &condExpr, &isTruthy))
    {
        reduct_expr_done(compiler, &condExpr);
        if (isTruthy)
        {
            reduct_expr_build(compiler, &then, out);
        }
        else if (list->length == 4)
        {
            reduct_expr_build(compiler, &els, out);
        }
        else
        {
            *out = REDUCT_EXPR_NIL(compiler);
        }
        return;
    }

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);

    reduct_reg_t condReg = reduct_compile_move_or_alloc(compiler, &condExpr);
    size_t jumpElse = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, condReg);
    reduct_expr_done(compiler, &condExpr);

    reduct_compile_build_into_target(compiler, &then, target);

    size_t jumpEnd = 0;
    if (list->length == 4)
    {
        jumpEnd = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
    }

    reduct_compile_jump_patch(compiler, jumpElse);

    if (list->length == 4)
    {
        reduct_compile_build_into_target(compiler, &els, target);
        reduct_compile_jump_patch(compiler, jumpEnd);
    }
    else
    {
        reduct_expr_t nilExpr = REDUCT_EXPR_NIL(compiler);
        reduct_compile_move(compiler, target, &nilExpr);
    }

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_cond(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    if (list->length < 2)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_reg_t target = REDUCT_REG_INVALID;
    size_t jumpsEnd[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;
    bool alwaysHit = false;

    reduct_handle_t pair;
    REDUCT_LIST_FOR_EACH_AT(&pair, list, 1)
    {
        reduct_list_t* pairList = reduct_intrinsic_get_pair(compiler, &pair, "cond");

        reduct_handle_t cond = reduct_list_first(compiler->reduct, pairList);
        reduct_handle_t body = reduct_list_second(compiler->reduct, pairList);

        reduct_expr_t condExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, &cond, &condExpr);

        bool isTruthy;
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
                reduct_expr_build(compiler, &body, out);
                return;
            }

            reduct_compile_build_into_target(compiler, &body, target);
            alwaysHit = true;
            break;
        }

        if (target == REDUCT_REG_INVALID)
        {
            target = (targetHint != REDUCT_REG_INVALID) ? targetHint : reduct_reg_alloc(compiler);
        }

        reduct_reg_t condReg = reduct_compile_move_or_alloc(compiler, &condExpr);
        size_t jumpNext = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, condReg);
        reduct_expr_done(compiler, &condExpr);

        reduct_compile_build_into_target(compiler, &body, target);

        REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX, "cond: too many clauses, limit is %u",
            REDUCT_REGISTER_MAX);
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

void reduct_intrinsic_match(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 3, "match: expected at least 2 arguments, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t target = reduct_list_nth(compiler->reduct, list, 1);

    reduct_expr_t targetExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &target, &targetExpr);

    reduct_handle_t targetHandle;
    bool targetKnown = reduct_expr_get_handle(compiler, &targetExpr, &targetHandle);
    reduct_reg_t targetReg = REDUCT_REG_INVALID;
    reduct_reg_t resultReg = reduct_expr_get_reg(compiler, out);

    size_t jumpsEnd[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_handle_t pair;
    REDUCT_LIST_FOR_EACH_AT(&pair, list, 2)
    {
        if (_iter.index >= list->length - 1)
        {
            break;
        }

        reduct_list_t* pairList = reduct_intrinsic_get_pair(compiler, &pair, "match");

        reduct_handle_t val = reduct_list_first(compiler->reduct, pairList);
        reduct_handle_t body = reduct_list_second(compiler->reduct, pairList);

        reduct_expr_t valExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, &val, &valExpr);

        reduct_handle_t valHandle;
        if (targetKnown && reduct_expr_get_handle(compiler, &valExpr, &valHandle))
        {
            bool cmpResult = false;
            if (reduct_fold_comparison(compiler->reduct, REDUCT_OPCODE_EQ, targetHandle, valHandle, &cmpResult))
            {
                reduct_expr_done(compiler, &valExpr);
                if (!cmpResult)
                {
                    continue;
                }

                reduct_compile_build_into_target(compiler, &body, resultReg);
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

        size_t jumpNext = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, cmpResultReg);
        reduct_reg_free(compiler, cmpResultReg);

        reduct_compile_build_into_target(compiler, &body, resultReg);

        REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX, "match: too many clauses, limit is %u",
            REDUCT_REGISTER_MAX);
        jumpsEnd[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
        reduct_compile_jump_patch(compiler, jumpNext);
    }

    reduct_handle_t last = reduct_list_nth(compiler->reduct, list, list->length - 1);
    reduct_compile_build_into_target(compiler, &last, resultReg);

    reduct_compile_jump_patch_list(compiler, jumpsEnd, jumpCount);

    reduct_expr_done(compiler, &targetExpr);
    *out = REDUCT_EXPR_REG(resultReg);
}

static void reduct_intrinsic_and_or(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out,
    reduct_opcode_t jumpOp)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    if (list->length < 2)
    {
        *out = REDUCT_EXPR_FALSE(compiler);
        return;
    }

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_reg_t target = REDUCT_REG_INVALID;
    size_t jumps[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_handle_t arg;
    REDUCT_LIST_FOR_EACH_AT(&arg, list, 1)
    {
        reduct_expr_t argExpr = REDUCT_EXPR_NONE();
        if (target != REDUCT_REG_INVALID)
        {
            argExpr = REDUCT_EXPR_TARGET(target);
        }

        reduct_expr_build(compiler, &arg, &argExpr);

        bool isTruthy;
        if (reduct_expr_is_known_truthy(compiler, &argExpr, &isTruthy))
        {
            bool shortCircuits = (jumpOp == REDUCT_OPCODE_JMPT) ? isTruthy : !isTruthy;

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
            REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX,
                "and/or: too many operands, limit is %u", REDUCT_REGISTER_MAX);
            jumps[jumpCount++] = reduct_compile_jump(compiler, jumpOp, target);
        }
    }

    reduct_compile_jump_patch_list(compiler, jumps, jumpCount);

    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_and(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_and_or(compiler, list, out, REDUCT_OPCODE_JMPF);
}

void reduct_intrinsic_or(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_and_or(compiler, list, out, REDUCT_OPCODE_JMPT);
}

void reduct_intrinsic_not(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 2, "not: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);

    reduct_handle_t arg = reduct_list_nth(compiler->reduct, list, 1);
    reduct_expr_t argExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &arg, &argExpr);

    bool isTruthy;
    if (reduct_expr_is_known_truthy(compiler, &argExpr, &isTruthy))
    {
        reduct_expr_done(compiler, &argExpr);
        *out = isTruthy ? REDUCT_EXPR_FALSE(compiler) : REDUCT_EXPR_TRUE(compiler);
        return;
    }

    reduct_reg_t argReg = reduct_compile_move_or_alloc(compiler, &argExpr);
    size_t jumpTrue = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPT, argReg);
    reduct_expr_done(compiler, &argExpr);

    reduct_expr_t trueExpr = REDUCT_EXPR_TRUE(compiler);
    reduct_compile_move(compiler, target, &trueExpr);

    size_t jumpEnd = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);

    reduct_compile_jump_patch(compiler, jumpTrue);
    reduct_expr_t falseExpr = REDUCT_EXPR_FALSE(compiler);
    reduct_compile_move(compiler, target, &falseExpr);

    reduct_compile_jump_patch(compiler, jumpEnd);

    *out = REDUCT_EXPR_REG(target);
}

static inline bool reduct_fold_binary_calc(reduct_opcode_t op, double lf, double rf, int64_t li, int64_t ri,
    bool isFloat, reduct_handle_t* out)
{
    if (isFloat)
    {
        switch (op)
        {
        case REDUCT_OPCODE_ADD:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf + rf);
            return true;
        case REDUCT_OPCODE_SUB:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf - rf);
            return true;
        case REDUCT_OPCODE_MUL:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf * rf);
            return true;
        case REDUCT_OPCODE_DIV:
            if (REDUCT_UNLIKELY(rf == 0.0))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_FLOAT(lf / rf);
            return true;
        default:
            return false;
        }
    }

    switch (op)
    {
    case REDUCT_OPCODE_ADD:
        *out = REDUCT_HANDLE_FROM_INT(li + ri);
        return true;
    case REDUCT_OPCODE_SUB:
        *out = REDUCT_HANDLE_FROM_INT(li - ri);
        return true;
    case REDUCT_OPCODE_MUL:
        *out = REDUCT_HANDLE_FROM_INT(li * ri);
        return true;
    case REDUCT_OPCODE_DIV:
        if (REDUCT_UNLIKELY(ri == 0))
        {
            return false;
        }
        *out = REDUCT_HANDLE_FROM_INT(li / ri);
        return true;
    case REDUCT_OPCODE_MOD:
        if (REDUCT_UNLIKELY(ri == 0))
        {
            return false;
        }
        *out = REDUCT_HANDLE_FROM_INT(li % ri);
        return true;
    case REDUCT_OPCODE_BAND:
        *out = REDUCT_HANDLE_FROM_INT(li & ri);
        return true;
    case REDUCT_OPCODE_BOR:
        *out = REDUCT_HANDLE_FROM_INT(li | ri);
        return true;
    case REDUCT_OPCODE_BXOR:
        *out = REDUCT_HANDLE_FROM_INT(li ^ ri);
        return true;
    case REDUCT_OPCODE_SHL:
        if (REDUCT_UNLIKELY(ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH))
        {
            return false;
        }
        *out = REDUCT_HANDLE_FROM_INT(li << ri);
        return true;
    case REDUCT_OPCODE_SHR:
        if (REDUCT_UNLIKELY(ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH))
        {
            return false;
        }
        *out = REDUCT_HANDLE_FROM_INT(li >> ri);
        return true;
    default:
        return false;
    }
}

static bool reduct_fold_binary_expr(reduct_compiler_t* compiler, reduct_opcode_t opBase, reduct_expr_t* leftExpr,
    reduct_expr_t* rightExpr, reduct_expr_t* outExpr)
{
    assert(compiler != NULL);
    assert(leftExpr != NULL);
    assert(rightExpr != NULL);

    assert(outExpr != NULL);
    if (leftExpr->mode != REDUCT_MODE_CONST || rightExpr->mode != REDUCT_MODE_CONST)
    {
        return false;
    }

    if (compiler->function->constants[leftExpr->constant].type != REDUCT_CONST_SLOT_TYPE_HANDLE ||
        compiler->function->constants[rightExpr->constant].type != REDUCT_CONST_SLOT_TYPE_HANDLE)
    {
        return false;
    }

    reduct_handle_t leftHandle = compiler->function->constants[leftExpr->constant].handle;
    reduct_handle_t rightHandle = compiler->function->constants[rightExpr->constant].handle;

    if (!REDUCT_HANDLE_IS_NUMBER_SHAPED(&leftHandle) || !REDUCT_HANDLE_IS_NUMBER_SHAPED(&rightHandle))
    {
        return false;
    }

    bool isFloat = REDUCT_HANDLE_IS_FLOAT_SHAPED(&leftHandle) || REDUCT_HANDLE_IS_FLOAT_SHAPED(&rightHandle);

    double lf = reduct_handle_as_float(compiler->reduct, &leftHandle);
    double rf = reduct_handle_as_float(compiler->reduct, &rightHandle);
    int64_t li = reduct_handle_as_int(compiler->reduct, &leftHandle);
    int64_t ri = reduct_handle_as_int(compiler->reduct, &rightHandle);

    reduct_handle_t result;
    if (!reduct_fold_binary_calc(opBase, lf, rf, li, ri, isFloat, &result))
    {
        return false;
    }

    *outExpr = REDUCT_EXPR_HANDLE(compiler, &result);
    return true;
}

void reduct_intrinsic_binary_generic(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out,
    reduct_opcode_t opBase)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    if (opBase == REDUCT_OPCODE_MOD || opBase == REDUCT_OPCODE_SHL || opBase == REDUCT_OPCODE_SHR)
    {
        REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 3, "operator: expected 2 arguments, got %zu",
            (size_t)list->length - 1);
    }
    else if (opBase >= REDUCT_OPCODE_BAND && opBase <= REDUCT_OPCODE_BXOR)
    {
        REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 3,
            "bitwise operator: expected at least 2 arguments, got %zu", (size_t)list->length - 1);
    }
    else
    {
        REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 2,
            "arithmetic operator: expected at least 1 argument, got %zu", (size_t)list->length - 1);
    }

    reduct_handle_t first = reduct_list_nth(compiler->reduct, list, 1);

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &first, &leftExpr);

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

    bool hasAccumulator = false;
    reduct_handle_t arg;
    REDUCT_LIST_FOR_EACH_AT(&arg, list, 2)
    {
        reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, &arg, &rightExpr);

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
            hasAccumulator = true;
        }
        else
        {
            reduct_compile_binary(compiler, opBase, leftExpr.reg, leftExpr.reg, &rightExpr);
        }

        reduct_expr_done(compiler, &rightExpr);
    }

    *out = leftExpr;
}

void reduct_intrinsic_add(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_ADD);
}

void reduct_intrinsic_sub(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SUB);
}

void reduct_intrinsic_mul(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_MUL);
}

void reduct_intrinsic_div(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_DIV);
}

void reduct_intrinsic_mod(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_MOD);
}

static void reduct_intrinsic_unary_op_generic(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out,
    reduct_opcode_t op, reduct_expr_t rightExpr, const char* name)
{
    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 2, "%s: expected 1 argument, got %zu", name,
        (size_t)list->length - 1);

    reduct_handle_t left = reduct_list_nth(compiler->reduct, list, 1);
    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &left, &leftExpr);

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

void reduct_intrinsic_inc(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_ADD, REDUCT_EXPR_INT(compiler, 1), "inc");
}

void reduct_intrinsic_dec(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_SUB, REDUCT_EXPR_INT(compiler, 1), "dec");
}

void reduct_intrinsic_bit_and(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BAND);
}

void reduct_intrinsic_bit_or(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BOR);
}

void reduct_intrinsic_bit_xor(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_BXOR);
}

void reduct_intrinsic_bit_not(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 2, "bitwise not: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    reduct_handle_t arg = reduct_list_nth(compiler->reduct, list, 1);
    reduct_expr_t argExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &arg, &argExpr);

    if (argExpr.mode == REDUCT_MODE_CONST)
    {
        reduct_handle_t argHandle;
        if (reduct_expr_get_handle(compiler, &argExpr, &argHandle))
        {
            if (REDUCT_HANDLE_IS_INT_SHAPED(&argHandle))
            {
                reduct_atom_t* result =
                    reduct_atom_new_int(compiler->reduct, ~reduct_handle_as_int(compiler->reduct, &argHandle));
                reduct_expr_done(compiler, &argExpr);
                *out = REDUCT_EXPR_ATOM(compiler, result);
                return;
            }
        }
    }

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_BNOT | argExpr.mode), target, 0, argExpr.value));
    reduct_expr_done(compiler, &argExpr);
    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_bit_shl(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SHL);
}

void reduct_intrinsic_bit_shr(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_binary_generic(compiler, list, out, REDUCT_OPCODE_SHR);
}

static void reduct_intrinsic_comparison_generic(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out,
    reduct_opcode_t opBase)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length >= 3, "comparison: expected at least 2 arguments, got %zu",
        (size_t)list->length - 1);

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);

    reduct_handle_t left = reduct_list_nth(compiler->reduct, list, 1);
    reduct_expr_t leftExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, &left, &leftExpr);

    reduct_reg_t target = REDUCT_REG_INVALID;
    size_t jumps[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH_AT(&handle, list, 2)
    {
        reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
        reduct_expr_build(compiler, &handle, &rightExpr);

        reduct_handle_t leftHandle, rightHandle;
        if (reduct_expr_get_handle(compiler, &leftExpr, &leftHandle) &&
            reduct_expr_get_handle(compiler, &rightExpr, &rightHandle))
        {
            bool cmpResult = false;
            if (reduct_fold_comparison(compiler->reduct, opBase, leftHandle, rightHandle, &cmpResult))
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
            REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX,
                "comparison: too many operands, limit is %u", REDUCT_REGISTER_MAX);
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

void reduct_intrinsic_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_EQ);
}

void reduct_intrinsic_exact_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_SEQ);
}

void reduct_intrinsic_not_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_NEQ);
}

void reduct_intrinsic_exact_not_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_SNEQ);
}

void reduct_intrinsic_less(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_LT);
}

void reduct_intrinsic_less_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_LE);
}

void reduct_intrinsic_greater(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_GT);
}

void reduct_intrinsic_greater_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_GE);
}

#define REDUCT_INTRINSIC_NATIVE_ARITH(_name, _op, _identity) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
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
        for (size_t i = 1; i < argc; i++) \
        { \
            REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &res, &argv[i], _op); \
        } \
        return res; \
    }

#define REDUCT_INTRINSIC_NATIVE_LOGIC(_name, _short_circuit_truth) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
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
            if (REDUCT_HANDLE_IS_TRUTHY(&res) == (_short_circuit_truth)) \
            { \
                return res; \
            } \
        } \
        return res; \
    }

#define REDUCT_INTRINSIC_NATIVE_BITWISE(_name, _op) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, #_op ": expected at least 2 argument(s), got %zu", \
            (size_t)argc); \
        int64_t res = reduct_handle_as_int(reduct, &argv[0]); \
        for (size_t i = 1; i < argc; i++) \
        { \
            res _op## = reduct_handle_as_int(reduct, &argv[i]); \
        } \
        return REDUCT_HANDLE_FROM_INT(res); \
    }

#define REDUCT_INTRINSIC_NATIVE_COMPARE(_name, _op) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
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

#define REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(_name, _expected) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        if (argc < 2) \
        { \
            return REDUCT_HANDLE_TRUE(); \
        } \
        for (size_t i = 0; i < argc - 1; i++) \
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

REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(seq, true)
REDUCT_INTRINSIC_NATIVE_COMPARE_STRICT(sneq, false)

REDUCT_INTRINSIC_NATIVE_LOGIC(and, false)
REDUCT_INTRINSIC_NATIVE_LOGIC(or, true)

static reduct_handle_t reduct_intrinsic_native_list(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    reduct_list_t* list = reduct_list_new(reduct);
    for (size_t i = 0; i < argc; i++)
    {
        reduct_list_push(reduct, list, argv[i]);
    }
    return REDUCT_HANDLE_FROM_LIST(list);
}

static reduct_handle_t reduct_intrinsic_native_div(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1, "/: expected at least 1 argument(s), got %zu", (size_t)argc);
    if (argc == 1)
    {
        reduct_handle_t res;
        reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
        REDUCT_HANDLE_DIV_FAST(reduct, &res, &one, &argv[0]);
        return res;
    }
    reduct_handle_t res = argv[0];
    for (size_t i = 1; i < argc; i++)
    {
        REDUCT_HANDLE_DIV_FAST(reduct, &res, &res, &argv[i]);
    }
    return res;
}

static reduct_handle_t reduct_intrinsic_native_mod(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "%%: expected 2 argument(s), got %zu", (size_t)argc);
    reduct_handle_t result;
    REDUCT_HANDLE_MOD_FAST(reduct, &result, &argv[0], &argv[1]);
    return result;
}

static reduct_handle_t reduct_intrinsic_native_inc(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "++: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &argv[0], &one, +);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_dec(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "--: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_INT(1);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, &argv[0], &one, -);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_bnot(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "~: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_FROM_INT(~reduct_handle_as_int(reduct, &argv[0]));
}

static reduct_handle_t reduct_intrinsic_native_shl(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "<<: expected 2 argument(s), got %zu", (size_t)argc);
    int64_t left = reduct_handle_as_int(reduct, &argv[0]);
    int64_t right = reduct_handle_as_int(reduct, &argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_INT_WIDTH)
    {
        REDUCT_ERROR_RUNTIME(reduct, "<<: shift amount must be 0-%lu, got %lld", (unsigned long)REDUCT_HANDLE_INT_WIDTH,
            (long long)right);
    }
    return REDUCT_HANDLE_FROM_INT(left << right);
}

static reduct_handle_t reduct_intrinsic_native_shr(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, ">>: expected 2 argument(s), got %zu", (size_t)argc);
    int64_t left = reduct_handle_as_int(reduct, &argv[0]);
    int64_t right = reduct_handle_as_int(reduct, &argv[1]);
    if (right < 0 || right >= REDUCT_HANDLE_INT_WIDTH)
    {
        REDUCT_ERROR_RUNTIME(reduct, ">>: shift amount must be 0-%lu, got %lld",
            (unsigned long)REDUCT_HANDLE_INT_WIDTH - 1, (long long)right);
    }
    return REDUCT_HANDLE_FROM_INT(left >> right);
}

static reduct_handle_t reduct_intrinsic_native_do(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    if (argc == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }
    return argv[argc - 1];
}

static reduct_handle_t reduct_intrinsic_native_not(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, "not: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_IS_TRUTHY(&argv[0]) ? REDUCT_HANDLE_FALSE() : REDUCT_HANDLE_TRUE();
}

static reduct_native_t reductIntrinsics[] = {
    {"quote", NULL, reduct_intrinsic_quote},
    {"list", reduct_intrinsic_native_list, reduct_intrinsic_list},
    {"do", reduct_intrinsic_native_do, reduct_intrinsic_do},
    {"lambda", NULL, reduct_intrinsic_lambda},
    {"->", NULL, reduct_intrinsic_thread},
    {"def", NULL, reduct_intrinsic_def},
    {"if", NULL, reduct_intrinsic_if},
    {"cond", NULL, reduct_intrinsic_cond},
    {"match", NULL, reduct_intrinsic_match},
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
    assert(reduct != NULL);

    reduct_native_register(reduct, reductIntrinsics, sizeof(reductIntrinsics) / sizeof(reductIntrinsics[0]));
}

REDUCT_API void reduct_intrinsic_block_generic(reduct_compiler_t* compiler, reduct_list_t* list, uint32_t startIdx,
    reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    if (startIdx >= list->length)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    uint16_t savedLocalCount = compiler->localCount;

    reduct_reg_t targetHint = REDUCT_EXPR_GET_TARGET(out);
    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH_AT(&handle, list, startIdx)
    {
        if (_iter.index + 1 == list->length)
        {
            if (targetHint != REDUCT_REG_INVALID)
            {
                reduct_compile_build_into_target(compiler, &handle, targetHint);
                *out = REDUCT_EXPR_REG(targetHint);
            }
            else
            {
                reduct_expr_build(compiler, &handle, out);
            }
        }
        else
        {
            reduct_expr_t expr = REDUCT_EXPR_NONE();
            reduct_expr_build(compiler, &handle, &expr);
            reduct_expr_done(compiler, &expr);
        }
    }

    reduct_local_pop(compiler, savedLocalCount, out);
}

REDUCT_API void reduct_intrinsic_list_generic(reduct_compiler_t* compiler, reduct_list_t* list, uint32_t startIdx,
    reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    uint32_t count = list->length - startIdx;
    if (count == 0)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_reg_t base = reduct_reg_alloc_range(compiler, count);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH_AT(&handle, list, startIdx)
    {
        size_t index = _iter.index - startIdx;
        reduct_expr_t elemExpr = REDUCT_EXPR_TARGET(base + index);
        reduct_expr_build(compiler, &handle, &elemExpr);
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
