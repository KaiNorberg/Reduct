#include <reduct/emit.h>
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/intrinsic.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <string.h>

REDUCT_API reduct_handle_t reduct_compile(reduct_t* reduct, reduct_handle_t ast)
{
    assert(reduct != NULL);

    reduct_function_t* func = reduct_function_new(reduct);

    reduct_compiler_t compiler;
    reduct_compiler_init(&compiler, reduct, func, NULL);

    if (REDUCT_HANDLE_IS_ITEM(ast))
    {
        reduct_item_t* astItem = REDUCT_HANDLE_TO_ITEM(ast);
        compiler.lastItem = astItem;
        reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);
        funcItem->inputId = astItem->inputId;
    }

    reduct_expr_t lastExpr = REDUCT_EXPR_NONE();
    reduct_expr_build(&compiler, ast, &lastExpr);

    reduct_compile_return(&compiler, &lastExpr);
    reduct_expr_done(&compiler, &lastExpr);

    reduct_compiler_deinit(&compiler);

    return REDUCT_HANDLE_FROM_FUNCTION(func);
}

REDUCT_API void reduct_compiler_init(reduct_compiler_t* compiler, reduct_t* reduct, reduct_function_t* function,
    reduct_compiler_t* enclosing)
{
    assert(compiler != NULL);
    assert(reduct != NULL);
    assert(function != NULL);

    compiler->enclosing = enclosing;
    compiler->reduct = reduct;
    compiler->function = function;
    compiler->localCount = 0;
    compiler->lastItem = enclosing != NULL ? enclosing->lastItem : NULL;

    memset(compiler->regAlloc, 0, sizeof(compiler->regAlloc));
    memset(compiler->regLocal, 0, sizeof(compiler->regLocal));
    memset(compiler->locals, 0, sizeof(compiler->locals));
}

REDUCT_API void reduct_compiler_deinit(reduct_compiler_t* compiler)
{
    assert(compiler != NULL);
    (void)compiler;
}

REDUCT_API reduct_reg_t reduct_reg_alloc(reduct_compiler_t* compiler)
{
    assert(compiler != NULL);

    size_t index = reduct_bitmap_find_first_clear(compiler->regAlloc, REDUCT_BITMAP_SIZE(REDUCT_REGISTER_MAX));
    if (index != REDUCT_BITMAP_INDEX_NONE)
    {
        reduct_reg_t reg = (reduct_reg_t)index;
        REDUCT_REG_SET_ALLOCATED(compiler, reg);
        return reg;
    }

    REDUCT_ERROR_COMPILE_LAST(compiler, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    return REDUCT_REGISTER_INVALID;
}

REDUCT_API reduct_reg_t reduct_reg_alloc_range(reduct_compiler_t* compiler, uint32_t count)
{
    assert(compiler != NULL);

    if (count == 0)
    {
        return 0;
    }

    for (uint32_t i = 0; i <= REDUCT_REGISTER_MAX - count; i++)
    {
        uint32_t length;
        for (length = 0; length < count; length++)
        {
            uint32_t reg = i + length;
            if (REDUCT_REG_IS_ALLOCATED(compiler, reg))
            {
                break;
            }
        }
        if (length == count)
        {
            for (uint32_t j = 0; j < count; j++)
            {
                uint32_t reg = i + j;
                REDUCT_REG_SET_ALLOCATED(compiler, reg);
            }
            return i;
        }
        i += length;
    }

    REDUCT_ERROR_COMPILE_LAST(compiler, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    return REDUCT_REGISTER_INVALID;
}

REDUCT_API void reduct_reg_free(reduct_compiler_t* compiler, reduct_reg_t reg)
{
    assert(compiler != NULL);
    if (reg >= REDUCT_REGISTER_MAX)
    {
        return;
    }

    if (REDUCT_REG_IS_LOCAL(compiler, reg))
    {
        return;
    }

    REDUCT_REG_CLEAR_ALLOCATED(compiler, reg);
}

REDUCT_API void reduct_reg_free_range(reduct_compiler_t* compiler, reduct_reg_t start, uint32_t count)
{
    assert(compiler != NULL);

    for (uint32_t i = 0; i < count; i++)
    {
        reduct_reg_free(compiler, start + i);
    }
}

static inline void reduct_expr_build_atom(reduct_compiler_t* compiler, reduct_handle_t handle, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(out != NULL);

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(handle))
    {
        *out = REDUCT_EXPR_HANDLE(compiler, handle);
        return;
    }

    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);

    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED || reduct_atom_is_intrinsic(compiler->reduct, atom) ||
        reduct_atom_is_native(compiler->reduct, atom))
    {
        *out = REDUCT_EXPR_ATOM(compiler, atom);
        return;
    }

    reduct_local_t* local = reduct_local_lookup(compiler, atom);
    if (local != NULL)
    {
        if (!REDUCT_LOCAL_IS_DEFINED(local))
        {
            REDUCT_ERROR_COMPILE(compiler, handle, "undefined local '%.*s'", atom->length, atom->string);
        }

        *out = local->expr;
        return;
    }

    switch (atom->length)
    {
    case 1:
        if (atom->string[0] == 'e')
        {
            *out = REDUCT_EXPR_E(compiler);
            return;
        }
        break;
    case 2:
        if (memcmp(atom->string, "pi", 2) == 0)
        {
            *out = REDUCT_EXPR_PI(compiler);
            return;
        }
        break;
    case 3:
        if (memcmp(atom->string, "nil", 3) == 0)
        {
            *out = REDUCT_EXPR_NIL(compiler);
            return;
        }
        break;
    case 4:
        if (memcmp(atom->string, "true", 4) == 0)
        {
            *out = REDUCT_EXPR_TRUE(compiler);
            return;
        }
        break;
    case 5:
        if (memcmp(atom->string, "false", 5) == 0)
        {
            *out = REDUCT_EXPR_FALSE(compiler);
            return;
        }
        break;
    }

    REDUCT_ERROR_COMPILE(compiler, handle, "unknown symbol '%.*s'", atom->length, atom->string);
}

static inline bool reduct_compiler_is_data(reduct_compiler_t* compiler, reduct_handle_t handle)
{
    assert(compiler != NULL);

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

        reduct_handle_t head = reduct_list_first(compiler->reduct, list);
        return reduct_compiler_is_data(compiler, head);
    }

    return false;
}

static inline void reduct_expr_build_list(reduct_compiler_t* compiler, reduct_list_t* list, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(list != NULL);
    assert(out != NULL);

    if (list->length == 0)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_handle_t head = reduct_list_first(compiler->reduct, list);
    if (reduct_compiler_is_data(compiler, head))
    {
        reduct_intrinsic_list_generic(compiler, list, 0, out);
        return;
    }

    if (REDUCT_HANDLE_IS_INTRINSIC(compiler->reduct, head))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(head);
        atom->intrinsic(compiler, list, out);
        return;
    }

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

    reduct_expr_t callable = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, head, &callable);

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

    reduct_compile_call(compiler, base, &callable, arity);

    reduct_expr_done(compiler, &callable);

    if (regCount > 1)
    {
        reduct_reg_free_range(compiler, base + 1, regCount - 1);
    }

    *out = REDUCT_EXPR_REG(base);
}

REDUCT_API void reduct_expr_build(reduct_compiler_t* compiler, reduct_handle_t handle, reduct_expr_t* out)
{
    assert(compiler != NULL);
    assert(out != NULL);

    reduct_item_t* previousItem = compiler->lastItem;
    if (REDUCT_HANDLE_IS_ITEM(handle))
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
        if (item->inputId != REDUCT_INPUT_ID_NONE)
        {
            compiler->lastItem = item;
        }
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_expr_build_atom(compiler, handle, out);
    }
    else if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_expr_build_list(compiler, REDUCT_HANDLE_TO_LIST(handle), out);
    }
    else
    {
        REDUCT_ERROR_COMPILE_LAST(compiler, "unexpected %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    }

    compiler->lastItem = previousItem;
}

REDUCT_API reduct_local_t* reduct_local_def(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    assert(compiler != NULL);
    assert(name != NULL);

    if (compiler->localCount >= REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE_LAST(compiler, "too many local variables in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    compiler->locals[compiler->localCount].name = name;
    compiler->locals[compiler->localCount].expr = REDUCT_EXPR_NONE();
    return &compiler->locals[compiler->localCount++];
}

REDUCT_API void reduct_local_def_done(reduct_compiler_t* compiler, reduct_local_t* local, reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(local != NULL);
    assert(expr != NULL);

    if (local->expr.mode != REDUCT_OPCODE_MODE_NONE)
    {
        return;
    }

    if (expr->mode == REDUCT_OPCODE_MODE_REG)
    {
        REDUCT_REG_SET_LOCAL(compiler, expr->reg);
    }

    local->expr = *expr;
}

REDUCT_API reduct_local_t* reduct_local_add_arg(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    assert(compiler != NULL);
    assert(name != NULL);

    if (compiler->localCount >= REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE_LAST(compiler, "too many local variables in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    reduct_reg_t reg = reduct_reg_alloc(compiler);
    compiler->locals[compiler->localCount].name = name;
    compiler->locals[compiler->localCount].expr = REDUCT_EXPR_REG(reg);
    REDUCT_REG_SET_LOCAL(compiler, reg);

    return &compiler->locals[compiler->localCount++];
}

REDUCT_API void reduct_local_pop(reduct_compiler_t* compiler, uint16_t toCount, reduct_expr_t* result)
{
    assert(compiler != NULL);
    reduct_reg_t resultReg = (result != NULL && result->mode == REDUCT_OPCODE_MODE_REG) ? result->reg : REDUCT_REGISTER_INVALID;

    for (uint32_t i = compiler->localCount; i > (uint32_t)toCount; i--)
    {
        reduct_local_t* local = &compiler->locals[i - 1];
        if (local->expr.mode == REDUCT_OPCODE_MODE_REG)
        {
            bool isResult = (local->expr.reg == resultReg);
            bool isOuterLocal = false;
            for (uint32_t j = 0; j < (uint32_t)toCount; j++)
            {
                if (compiler->locals[j].expr.mode == REDUCT_OPCODE_MODE_REG && compiler->locals[j].expr.reg == local->expr.reg)
                {
                    isOuterLocal = true;
                    break;
                }
            }

            if (!isOuterLocal)
            {
                REDUCT_REG_CLEAR_LOCAL(compiler, local->expr.reg);
                if (!isResult)
                {
                    REDUCT_REG_CLEAR_ALLOCATED(compiler, local->expr.reg);
                }
            }
        }
        local->name = NULL;
        local->expr = REDUCT_EXPR_NONE();
    }
    compiler->localCount = toCount;
}

REDUCT_API reduct_local_t* reduct_local_lookup(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    assert(compiler != NULL);
    assert(name != NULL);

    for (int16_t i = compiler->localCount - 1; i >= 0; i--)
    {
        if (compiler->locals[i].name == name)
        {
            return &compiler->locals[i];
        }
    }

    reduct_compiler_t* current = compiler->enclosing;
    while (current != NULL)
    {
        for (int16_t i = current->localCount - 1; i >= 0; i--)
        {
            if (current->locals[i].name != name)
            {
                continue;
            }

            if (current->locals[i].expr.mode == REDUCT_OPCODE_MODE_CONST)
            {
                reduct_const_t constant = reduct_function_lookup_constant(compiler->reduct, compiler->function,
                    &current->function->constants[current->locals[i].expr.constant]);
                reduct_expr_t constExpr = REDUCT_EXPR_CONST(constant);

                reduct_local_t* local = reduct_local_def(compiler, name);
                reduct_local_def_done(compiler, local, &constExpr);
                return local;
            }

            reduct_const_slot_t slot = REDUCT_CONST_SLOT_CAPTURE(name);
            reduct_const_t constant = reduct_function_lookup_constant(compiler->reduct, compiler->function, &slot);
            reduct_expr_t constExpr = REDUCT_EXPR_CONST(constant);

            reduct_local_t* local = reduct_local_def(compiler, name);
            reduct_local_def_done(compiler, local, &constExpr);
            return local;
        }
        current = current->enclosing;
    }

    return NULL;
}

REDUCT_API void reduct_compile_return(reduct_compiler_t* compiler, reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(expr != NULL);

    if (expr->mode == REDUCT_OPCODE_MODE_NONE)
    {
        reduct_expr_t nilExpr = REDUCT_EXPR_NIL(compiler);
        uint32_t pos = compiler->lastItem != NULL ? compiler->lastItem->position : 0;
        reduct_function_emit(compiler->reduct, compiler->function,
            REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RET | REDUCT_OPCODE_MODE_CONST, 0, 0, nilExpr.constant), pos);
        return;
    }

    assert(expr->mode == REDUCT_OPCODE_MODE_REG || expr->mode == REDUCT_OPCODE_MODE_CONST);
    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_RET | (reduct_opcode_t)expr->mode), 0, 0, expr->value));
}