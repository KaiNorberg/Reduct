#include <reduct/intrinsic.h>
#include <reduct/atom.h>
#include <reduct/defs.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/standard.h>

void reduct_intrinsic_quote(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, list->length == 2, "quote: expected 1 argument, got %zu",
        (size_t)list->length - 1);

    *out = REDUCT_EXPR_HANDLE(compiler, reduct_list_nth(compiler->reduct, list, 1));
}

void reduct_intrinsic_list(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    return reduct_intrinsic_list_generic(compiler, list, 1, out);
}

static inline void reduct_compile_build_into_target(reduct_compiler_t* compiler, reduct_handle_t handle,
    reduct_reg_t target)
{
    reduct_expr_t expr = REDUCT_EXPR_TARGET(target);
    reduct_expr_build(compiler, handle, &expr);
    if (expr.mode == REDUCT_OPCODE_MODE_NONE)
    {
        reduct_expr_t nil = REDUCT_EXPR_NIL(compiler);
        reduct_compile_move(compiler, target, &nil);
    }
    else if (expr.mode != REDUCT_OPCODE_MODE_REG || expr.reg != target)
    {
        reduct_compile_move(compiler, target, &expr);
        reduct_expr_done(compiler, &expr);
    }
}

void reduct_intrinsic_recur(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    uint32_t arity = (uint32_t)list->length - 1;
    uint32_t regCount = arity == 0 ? 1 : arity;

    reduct_reg_t base = reduct_reg_get_base(compiler);

    if (base + regCount > REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE_LAST(compiler, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    for (uint32_t i = 0; i < regCount; i++)
    {
        REDUCT_REG_SET_ALLOCATED(compiler, base + i);
    }

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 1);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_handle_t argH = chunk.handles[i];
            reduct_reg_t target = (reduct_reg_t)(base + baseIdx + i - 1);
            reduct_expr_t argExpr = REDUCT_EXPR_TARGET(target);
            reduct_expr_build(compiler, argH, &argExpr);

            if (argExpr.mode != REDUCT_OPCODE_MODE_REG || argExpr.reg != target)
            {
                reduct_compile_move(compiler, target, &argExpr);
                reduct_expr_done(compiler, &argExpr);
            }
        }
    }

    reduct_compile_recur(compiler, base, arity);

    if (regCount > 1)
    {
        reduct_reg_free_range(compiler, base + 1, regCount - 1);
    }

    *out = REDUCT_EXPR_REG(base);
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
    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_LIST(args), "lambda: parameter list must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(args));

    reduct_list_t* argsList = REDUCT_HANDLE_TO_LIST(args);
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

    reduct_list_iter_t iterArgs = REDUCT_LIST_ITER(argsList);
    reduct_list_chunk_t chunkArgs;
    while (reduct_list_iter_next_chunk(&iterArgs, &chunkArgs))
    {
        size_t baseIdx = iterArgs.index - chunkArgs.count;
        for (size_t i = 0; i < chunkArgs.count; i++)
        {
            reduct_handle_t arg = chunkArgs.handles[i];
            REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_ATOM(arg),
                "lambda: the name of each parameter must be an atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(arg));

            reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(arg);
            if (atom->length > 0 && atom->string[0] == '*')
            {
                REDUCT_ERROR_COMPILE_ASSERT(compiler, baseIdx + i == argsList->length - 1,
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
        REDUCT_ERROR_COMPILE_ASSERT(compiler, captured != NULL, "unable to capture '%.*s'", captureName->length,
            captureName->string);

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
    reduct_expr_build(compiler, first, &current);

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 2);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_handle_t step = chunk.handles[i];
            reduct_handle_t head;
            uint32_t arity;

            if (REDUCT_HANDLE_IS_LIST(step))
            {
                reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(step);
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

            if (REDUCT_HANDLE_IS_LIST(step))
            {
                reduct_list_t* stepList = REDUCT_HANDLE_TO_LIST(step);
                reduct_list_iter_t stepIter = REDUCT_LIST_ITER_AT(stepList, 1);
                reduct_list_chunk_t stepChunk;
                while (reduct_list_iter_next_chunk(&stepIter, &stepChunk))
                {
                    size_t stepBaseIdx = stepIter.index - stepChunk.count;
                    for (size_t j = 0; j < stepChunk.count; j++)
                    {
                        reduct_reg_t argReg = (reduct_reg_t)(base + stepBaseIdx + j);
                        reduct_expr_t argExpr = REDUCT_EXPR_TARGET(argReg);
                        reduct_expr_build(compiler, stepChunk.handles[j], &argExpr);

                        if (argExpr.mode != REDUCT_OPCODE_MODE_REG || argExpr.reg != argReg)
                        {
                            reduct_compile_move(compiler, argReg, &argExpr);
                            reduct_expr_done(compiler, &argExpr);
                        }
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
    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_ATOM(name), "def: name must be an atom, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(name));
    reduct_handle_t val = reduct_list_nth(compiler->reduct, list, 2);

    reduct_local_t* local = reduct_local_def(compiler, REDUCT_HANDLE_TO_ATOM(name));

    reduct_expr_t valExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, val, &valExpr);

    reduct_local_def_done(compiler, local, &valExpr);
    reduct_expr_done(compiler, &valExpr);

    *out = valExpr;
}

static reduct_list_t* reduct_intrinsic_get_pair(reduct_compiler_t* compiler, reduct_handle_t handle, const char* name)
{
    assert(compiler != NULL);

    REDUCT_ERROR_COMPILE_ASSERT(compiler, REDUCT_HANDLE_IS_LIST(handle), "%s: each clause must be a list, got %s", name,
        REDUCT_HANDLE_GET_TYPE_STRING(handle));

    reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(handle);
    REDUCT_ERROR_COMPILE_ASSERT(compiler, pair->length == 2, "%s: each clause must be a list of 2 items, got length %u",
        name, pair->length);

    return pair;
}

static inline reduct_opcode_t reduct_cmp_to_skip_op(reduct_opcode_t cmpBase)
{
    switch (cmpBase)
    {
    case REDUCT_OPCODE_EQ:
        return REDUCT_OPCODE_JEQ;
    case REDUCT_OPCODE_NEQ:
        return REDUCT_OPCODE_JNEQ;
    case REDUCT_OPCODE_LT:
        return REDUCT_OPCODE_JLT;
    case REDUCT_OPCODE_LE:
        return REDUCT_OPCODE_JLE;
    case REDUCT_OPCODE_GT:
        return REDUCT_OPCODE_JGT;
    case REDUCT_OPCODE_GE:
        return REDUCT_OPCODE_JGE;
    default:
        return REDUCT_OPCODE_LIST;
    }
}

void reduct_intrinsic_less(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);
void reduct_intrinsic_less_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);
void reduct_intrinsic_greater(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);
void reduct_intrinsic_greater_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);
void reduct_intrinsic_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);
void reduct_intrinsic_not_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out);

static size_t reduct_compile_branch(reduct_compiler_t* compiler, reduct_handle_t cond)
{
    reduct_expr_t condExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, cond, &condExpr);

    reduct_reg_t condReg = reduct_compile_move_or_alloc(compiler, &condExpr);
    size_t jumpElse = reduct_compile_jump(compiler, REDUCT_OPCODE_JMPF, condReg);
    reduct_expr_done(compiler, &condExpr);

    return jumpElse;
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
    reduct_handle_t els =
        (list->length == 4) ? reduct_list_nth(compiler->reduct, list, 3) : REDUCT_HANDLE_NIL(compiler->reduct);

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    size_t jumpElse = reduct_compile_branch(compiler, cond);
    reduct_compile_build_into_target(compiler, then, target);

    size_t jumpEnd = 0;
    if (list->length == 4)
    {
        jumpEnd = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
    }

    if (jumpElse != REDUCT_JUMP_INVALID)
    {
        reduct_compile_jump_patch(compiler, jumpElse);
    }

    if (list->length == 4)
    {
        reduct_compile_build_into_target(compiler, els, target);
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

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    size_t jumpsEnd[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;
    bool alwaysHit = false;

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 1);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_list_t* pairList = reduct_intrinsic_get_pair(compiler, chunk.handles[i], "cond");

            reduct_handle_t cond = reduct_list_first(compiler->reduct, pairList);
            reduct_handle_t body = reduct_list_second(compiler->reduct, pairList);

            size_t jumpNext = reduct_compile_branch(compiler, cond);
            if (jumpNext == REDUCT_JUMP_INVALID)
            {
                alwaysHit = true;
            }

            reduct_compile_build_into_target(compiler, body, target);

            REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX,
                "cond: too many clauses, limit is %u", REDUCT_REGISTER_MAX);
            jumpsEnd[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);

            if (jumpNext != REDUCT_JUMP_INVALID)
            {
                reduct_compile_jump_patch(compiler, jumpNext);
            }
        }
        if (alwaysHit)
        {
            break;
        }
    }

    if (target == REDUCT_REGISTER_INVALID)
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
    reduct_expr_build(compiler, target, &targetExpr);

    reduct_reg_t targetReg = REDUCT_REGISTER_INVALID;
    reduct_reg_t resultReg = reduct_expr_get_reg(compiler, out);

    size_t jumpsEnd[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 2);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            if (baseIdx + i >= list->length - 1)
            {
                break;
            }

            reduct_list_t* pairList = reduct_intrinsic_get_pair(compiler, chunk.handles[i], "match");

            reduct_handle_t val = reduct_list_first(compiler->reduct, pairList);
            reduct_handle_t body = reduct_list_second(compiler->reduct, pairList);

            reduct_expr_t valExpr = REDUCT_EXPR_NONE();
            reduct_expr_build(compiler, val, &valExpr);

            if (targetReg == REDUCT_REGISTER_INVALID)
            {
                targetReg = reduct_compile_move_or_alloc(compiler, &targetExpr);
            }

            reduct_compile_skip(compiler, REDUCT_OPCODE_JEQ, targetReg, &valExpr);
            reduct_expr_done(compiler, &valExpr);

            size_t jumpNext = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);

            reduct_compile_build_into_target(compiler, body, resultReg);

            REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX,
                "match: too many clauses, limit is %u", REDUCT_REGISTER_MAX);
            jumpsEnd[jumpCount++] = reduct_compile_jump(compiler, REDUCT_OPCODE_JMP, 0);
            reduct_compile_jump_patch(compiler, jumpNext);
        }
    }

    reduct_handle_t last = reduct_list_nth(compiler->reduct, list, list->length - 1);
    reduct_compile_build_into_target(compiler, last, resultReg);

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
    reduct_reg_t target = REDUCT_REGISTER_INVALID;
    size_t jumps[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 1);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        bool broken = false;
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_expr_t argExpr = REDUCT_EXPR_NONE();
            if (target != REDUCT_REGISTER_INVALID)
            {
                argExpr = REDUCT_EXPR_TARGET(target);
            }

            reduct_expr_build(compiler, chunk.handles[i], &argExpr);

            if (target == REDUCT_REGISTER_INVALID)
            {
                target = (targetHint != REDUCT_REGISTER_INVALID) ? targetHint : reduct_reg_alloc(compiler);
            }

            if (argExpr.mode != REDUCT_OPCODE_MODE_REG || argExpr.reg != target)
            {
                reduct_compile_move(compiler, target, &argExpr);
                reduct_expr_done(compiler, &argExpr);
            }

            if (baseIdx + i + 1 != list->length)
            {
                REDUCT_ERROR_COMPILE_ASSERT(compiler, jumpCount < REDUCT_REGISTER_MAX,
                    "and/or: too many operands, limit is %u", REDUCT_REGISTER_MAX);
                jumps[jumpCount++] = reduct_compile_jump(compiler, jumpOp, target);
            }
        }
        if (broken)
        {
            break;
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
    reduct_expr_build(compiler, arg, &argExpr);

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
    reduct_expr_build(compiler, first, &leftExpr);

    if (list->length == 2)
    {
        if (opBase == REDUCT_OPCODE_SUB || opBase == REDUCT_OPCODE_DIV)
        {
            reduct_expr_t initialExpr =
                (opBase == REDUCT_OPCODE_SUB) ? REDUCT_EXPR_NUMBER(compiler, 0.0) : REDUCT_EXPR_NUMBER(compiler, 1.0);
            reduct_reg_t initialReg = reduct_compile_move_or_alloc(compiler, &initialExpr);
            reduct_reg_t target = targetHint;
            reduct_compile_binary(compiler, opBase, &target, initialReg, &leftExpr);

            reduct_expr_done(compiler, &leftExpr);
            reduct_expr_done(compiler, &initialExpr);
            *out = REDUCT_EXPR_REG(target);
            return;
        }

        *out = leftExpr;
        return;
    }

    bool hasAccumulator = false;
    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 2);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
            reduct_expr_build(compiler, chunk.handles[i], &rightExpr);

            if (!hasAccumulator)
            {
                if (leftExpr.mode != REDUCT_OPCODE_MODE_REG)
                {
                    reduct_compile_move_or_alloc(compiler, &leftExpr);
                }

                reduct_reg_t target = targetHint;
                reduct_compile_binary(compiler, opBase, &target, leftExpr.reg, &rightExpr);

                reduct_expr_done(compiler, &leftExpr);
                leftExpr = REDUCT_EXPR_REG(target);
                hasAccumulator = true;
            }
            else
            {
                reduct_reg_t target = leftExpr.reg;
                reduct_compile_binary(compiler, opBase, &target, leftExpr.reg, &rightExpr);
            }

            reduct_expr_done(compiler, &rightExpr);
        }
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
    reduct_expr_build(compiler, left, &leftExpr);

    reduct_reg_t target = reduct_expr_get_reg(compiler, out);
    reduct_compile_binary(compiler, op, &target, reduct_compile_move_or_alloc(compiler, &leftExpr), &rightExpr);

    reduct_expr_done(compiler, &leftExpr);
    reduct_expr_done(compiler, &rightExpr);
    *out = REDUCT_EXPR_REG(target);
}

void reduct_intrinsic_inc(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_ADD, REDUCT_EXPR_NUMBER(compiler, 1.0), "inc");
}

void reduct_intrinsic_dec(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_unary_op_generic(compiler, list, out, REDUCT_OPCODE_SUB, REDUCT_EXPR_NUMBER(compiler, 1.0), "dec");
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
    reduct_expr_build(compiler, arg, &argExpr);

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
    reduct_expr_build(compiler, left, &leftExpr);

    reduct_reg_t target = REDUCT_REGISTER_INVALID;
    size_t jumps[REDUCT_REGISTER_MAX];
    size_t jumpCount = 0;

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, 2);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_expr_t rightExpr = REDUCT_EXPR_NONE();
            reduct_expr_build(compiler, chunk.handles[i], &rightExpr);

            if (target == REDUCT_REGISTER_INVALID)
            {
                target = targetHint;
            }

            if (leftExpr.mode != REDUCT_OPCODE_MODE_REG)
            {
                reduct_compile_move_or_alloc(compiler, &leftExpr);
            }

            reduct_compile_binary(compiler, opBase, &target, leftExpr.reg, &rightExpr);

            if (baseIdx + i + 1 != list->length)
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
    }

    reduct_expr_done(compiler, &leftExpr);

    if (jumpCount > 0)
    {
        reduct_compile_jump_patch_list(compiler, jumps, jumpCount);
        *out = REDUCT_EXPR_REG(target);
    }
    else if (target == REDUCT_REGISTER_INVALID)
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

void reduct_intrinsic_not_equal(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    reduct_intrinsic_comparison_generic(compiler, list, out, REDUCT_OPCODE_NEQ);
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

#define REDUCT_INTRINSIC_NATIVE_LOGIC(_name, _shortCircuitTruth) \
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
            if (REDUCT_HANDLE_IS_TRUTHY(res) == (_shortCircuitTruth)) \
            { \
                return res; \
            } \
        } \
        return res; \
    }

#define REDUCT_INTRINSIC_NATIVE_BITWISE(_name, _op) \
    static reduct_handle_t reduct_intrinsic_native_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 2, #_op ": expected at least 2 argument(s), got %zu", (size_t)argc); \
        int64_t res = reduct_handle_as_int(reduct, argv[0]); \
        for (size_t i = 1; i < argc; i++) \
        { \
            res _op## = reduct_handle_as_int(reduct, argv[i]); \
        } \
        return REDUCT_HANDLE_FROM_NUMBER((double)res); \
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

static reduct_handle_t reduct_intrinsic_native_mod(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 2, "%%: expected 2 argument(s), got %zu", (size_t)argc);
    reduct_handle_t result;
    REDUCT_HANDLE_MOD_FAST(reduct, &result, argv[0], argv[1]);
    return result;
}

static reduct_handle_t reduct_intrinsic_native_inc(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "++: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_NUMBER(1.0);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, argv[0], one, +);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_dec(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "--: expected 1 argument(s), got %zu", (size_t)argc);
    reduct_handle_t res;
    reduct_handle_t one = REDUCT_HANDLE_FROM_NUMBER(1.0);
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &res, argv[0], one, -);
    return res;
}

static reduct_handle_t reduct_intrinsic_native_bnot(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "~: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_FROM_NUMBER((double)(~reduct_handle_as_int(reduct, argv[0])));
}

static reduct_handle_t reduct_intrinsic_native_shl(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
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

static reduct_handle_t reduct_intrinsic_native_shr(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
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
    REDUCT_ERROR_ASSERT(reduct, argc == 1, "not: expected 1 argument(s), got %zu", (size_t)argc);
    return REDUCT_HANDLE_IS_TRUTHY(argv[0]) ? REDUCT_HANDLE_FALSE() : REDUCT_HANDLE_TRUE();
}

static reduct_native_t reductIntrinsics[] = {
    {"quote", NULL, reduct_intrinsic_quote},
    {"recur", NULL, reduct_intrinsic_recur},
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
    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, startIdx);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            reduct_handle_t handle = chunk.handles[i];
            if (baseIdx + i + 1 == list->length)
            {
                if (targetHint != REDUCT_REGISTER_INVALID)
                {
                    reduct_compile_build_into_target(compiler, handle, targetHint);
                    *out = REDUCT_EXPR_REG(targetHint);
                }
                else
                {
                    reduct_expr_build(compiler, handle, out);
                }
            }
            else
            {
                reduct_expr_t expr = REDUCT_EXPR_NONE();
                reduct_expr_build(compiler, handle, &expr);
                reduct_expr_done(compiler, &expr);
            }
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

    reduct_list_iter_t iter = REDUCT_LIST_ITER_AT(list, startIdx);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk))
    {
        size_t baseIdx = iter.index - chunk.count;
        for (size_t i = 0; i < chunk.count; i++)
        {
            size_t index = baseIdx + i - startIdx;
            reduct_expr_t elemExpr = REDUCT_EXPR_TARGET((reduct_reg_t)(base + index));
            reduct_expr_build(compiler, chunk.handles[i], &elemExpr);
            if (elemExpr.mode != REDUCT_OPCODE_MODE_REG || elemExpr.reg != (reduct_reg_t)(base + index))
            {
                reduct_compile_move(compiler, (reduct_reg_t)(base + index), &elemExpr);
                reduct_expr_done(compiler, &elemExpr);
            }
        }
    }

    reduct_compile_list(compiler, base, count);
    reduct_reg_free_range(compiler, base + 1, count - 1);

    *out = REDUCT_EXPR_REG(base);
}
