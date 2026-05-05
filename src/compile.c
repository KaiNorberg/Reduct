#include "reduct/inst.h"
#include "reduct/compile.h"
#include "reduct/core.h"
#include "reduct/gc.h"
#include "reduct/intrinsic.h"
#include "reduct/item.h"
#include "reduct/list.h"

REDUCT_API reduct_function_t* reduct_compile(reduct_t* reduct, reduct_handle_t* ast)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(ast != REDUCT_NULL);

    reduct_function_t* func = reduct_function_new(reduct);
    reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);

    reduct_compiler_t compiler;
    reduct_compiler_init(&compiler, reduct, func, REDUCT_NULL);

    reduct_item_t* astItem = REDUCT_HANDLE_TO_ITEM(ast);
    compiler.lastItem = astItem;
    funcItem->inputId = astItem->inputId;

    reduct_expr_t lastExpr = REDUCT_EXPR_NONE();
    reduct_intrinsic_block_generic(&compiler, astItem, 0, &lastExpr);

    reduct_compile_return(&compiler, &lastExpr);
    reduct_expr_done(&compiler, &lastExpr);

    reduct_compiler_deinit(&compiler);

    return func;
}

REDUCT_API void reduct_compiler_init(reduct_compiler_t* compiler, reduct_t* reduct, reduct_function_t* function,
    reduct_compiler_t* enclosing)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(function != REDUCT_NULL);

    compiler->enclosing = enclosing;
    compiler->reduct = reduct;
    compiler->function = function;
    compiler->localCount = 0;
    compiler->lastItem = enclosing != REDUCT_NULL ? enclosing->lastItem : REDUCT_NULL;

    REDUCT_MEMSET(compiler->regAlloc, 0, sizeof(compiler->regAlloc));
    REDUCT_MEMSET(compiler->regLocal, 0, sizeof(compiler->regLocal));
    REDUCT_MEMSET(compiler->locals, 0, sizeof(compiler->locals));
}

REDUCT_API void reduct_compiler_deinit(reduct_compiler_t* compiler)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    (void)compiler;
}

REDUCT_API reduct_reg_t reduct_reg_alloc(reduct_compiler_t* compiler)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);

    reduct_size_t index = reduct_bitmap_find_first_clear(compiler->regAlloc, REDUCT_BITMAP_SIZE(REDUCT_REGISTER_MAX));
    if (index != REDUCT_BITMAP_INDEX_NONE)
    {
        reduct_reg_t reg = (reduct_reg_t)index;
        REDUCT_REG_SET_ALLOCATED(compiler, reg);
        return reg;
    }

    REDUCT_ERROR_COMPILE(compiler, compiler->lastItem, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    return REDUCT_REG_INVALID;
}

REDUCT_API reduct_reg_t reduct_reg_alloc_range(reduct_compiler_t* compiler, reduct_uint32_t count)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);

    if (count == 0)
    {
        return 0;
    }

    for (reduct_uint32_t i = 0; i <= REDUCT_REGISTER_MAX - count; i++)
    {
        reduct_uint32_t length;
        for (length = 0; length < count; length++)
        {
            reduct_uint32_t reg = i + length;
            if (REDUCT_REG_IS_ALLOCATED(compiler, reg))
            {
                break;
            }
        }
        if (length == count)
        {
            for (reduct_uint32_t j = 0; j < count; j++)
            {
                reduct_uint32_t reg = i + j;
                REDUCT_REG_SET_ALLOCATED(compiler, reg);
            }
            return i;
        }
        i += length;
    }

    REDUCT_ERROR_COMPILE(compiler, compiler->lastItem, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    return REDUCT_REG_INVALID;
}

REDUCT_API void reduct_reg_free(reduct_compiler_t* compiler, reduct_reg_t reg)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
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

REDUCT_API void reduct_reg_free_range(reduct_compiler_t* compiler, reduct_reg_t start, reduct_uint32_t count)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);

    for (reduct_uint32_t i = 0; i < count; i++)
    {
        reduct_reg_free(compiler, start + i);
    }
}

static inline void reduct_expr_build_atom(reduct_compiler_t* compiler, reduct_item_t* atom, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(atom != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (atom->flags & REDUCT_ITEM_FLAG_QUOTED || reduct_atom_is_intrinsic(compiler->reduct, &atom->atom) || reduct_atom_is_number(&atom->atom) || reduct_atom_is_native(compiler->reduct, &atom->atom))
    {
        *out = REDUCT_EXPR_CONST_ITEM(compiler, atom);
        return;
    }

    reduct_local_t* local = reduct_local_lookup(compiler, &atom->atom);
    if (local != REDUCT_NULL)
    {
        if (!REDUCT_LOCAL_IS_DEFINED(local))
        {
            REDUCT_ERROR_COMPILE(compiler, atom, "undefined local '%.*s'", atom->atom.length, atom->atom.string);
        }

        *out = local->expr;
        return;
    }

    for (reduct_uint32_t i = 0; i < compiler->reduct->constantCount; i++)
    {
        if (&atom->atom == compiler->reduct->constants[i].name)
        {
            *out = REDUCT_EXPR_CONST_ITEM(compiler, compiler->reduct->constants[i].item);
            return;
        }
    }

    REDUCT_ERROR_COMPILE(compiler, atom, "undefined local '%.*s'", atom->atom.length, atom->atom.string);
}

static inline reduct_bool_t reduct_compiler_is_data(reduct_compiler_t* compiler, reduct_item_t* item)
{
    if (item->flags & REDUCT_ITEM_FLAG_QUOTED)
    {
        return REDUCT_TRUE;
    }

    if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        return reduct_atom_is_number(&item->atom);
    }

    if (item->type == REDUCT_ITEM_TYPE_LIST)
    {
        if (item->length == 0)
        {
            return REDUCT_TRUE;
        }

        reduct_item_t* head = reduct_list_nth_item(compiler->reduct, &item->list, 0);
        return reduct_compiler_is_data(compiler, head);
    }

    return REDUCT_FALSE;
}

static inline void reduct_expr_build_list(reduct_compiler_t* compiler, reduct_item_t* list, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(list != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    if (list->flags & REDUCT_ITEM_FLAG_QUOTED)
    {
        *out = REDUCT_EXPR_CONST_ITEM(compiler, list);
        return;
    }

    if (list->length == 0)
    {
        *out = REDUCT_EXPR_NIL(compiler);
        return;
    }

    reduct_item_t* head = reduct_list_nth_item(compiler->reduct, &list->list, 0);
    if (reduct_compiler_is_data(compiler, head))
    {
        reduct_intrinsic_list_generic(compiler, list, 0, out);
        return;
    }

    if (head->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_intrinsic(compiler->reduct, &head->atom))
    {
        head->atom.intrinsic(compiler, list, out);
        return;
    }

    reduct_uint32_t arity = (reduct_uint32_t)list->length - 1;
    reduct_uint32_t regCount = arity == 0 ? 1 : arity;

    reduct_reg_t base = reduct_reg_get_base(compiler);

    if (base + regCount > REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE(compiler, list, "too many registers in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    for (reduct_uint32_t i = 0; i < regCount; i++)
    {
        REDUCT_REG_SET_ALLOCATED(compiler, base + i);
    }

    reduct_expr_t callable = REDUCT_EXPR_NONE();
    reduct_expr_build(compiler, head, &callable);

    reduct_handle_t argH;
    REDUCT_LIST_FOR_EACH_AT(&argH, &list->list, 1)
    {
        reduct_reg_t target = (reduct_reg_t)(base + _iter.index - 1);
        reduct_expr_t argExpr = REDUCT_EXPR_TARGET(target);
        reduct_expr_build(compiler, REDUCT_HANDLE_TO_ITEM(&argH), &argExpr);

        if (argExpr.mode != REDUCT_MODE_REG || argExpr.reg != target)
        {
            reduct_compile_move(compiler, target, &argExpr);
            reduct_expr_done(compiler, &argExpr);
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

REDUCT_API void reduct_expr_build(reduct_compiler_t* compiler, reduct_item_t* item, reduct_expr_t* out)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(item != REDUCT_NULL);
    REDUCT_ASSERT(out != REDUCT_NULL);

    reduct_item_t* previousItem = compiler->lastItem;
    if (item != REDUCT_NULL && item->inputId != REDUCT_INPUT_ID_NONE)
    {
        compiler->lastItem = item;
    }

    if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        reduct_expr_build_atom(compiler, item, out);
    }
    else
    {
        reduct_expr_build_list(compiler, item, out);
    }

    compiler->lastItem = previousItem;
}

REDUCT_API reduct_local_t* reduct_local_def(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(name != REDUCT_NULL);

    if (compiler->localCount >= REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE(compiler, compiler->lastItem, "too many local variables in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    compiler->locals[compiler->localCount].name = name;
    compiler->locals[compiler->localCount].expr = REDUCT_EXPR_NONE();
    return &compiler->locals[compiler->localCount++];
}

REDUCT_API void reduct_local_def_done(reduct_compiler_t* compiler, reduct_local_t* local, reduct_expr_t* expr)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(local != REDUCT_NULL);
    REDUCT_ASSERT(expr != REDUCT_NULL);

    if (local->expr.mode != REDUCT_MODE_NONE)
    {
        return;
    }

    if (expr->mode == REDUCT_MODE_REG)
    {
        REDUCT_REG_SET_LOCAL(compiler, expr->reg);
    }

    local->expr = *expr;
}

REDUCT_API reduct_local_t* reduct_local_add_arg(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(name != REDUCT_NULL);

    if (compiler->localCount >= REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_COMPILE(compiler, compiler->lastItem, "too many local variables in function, limit is %u", REDUCT_REGISTER_MAX);
    }

    reduct_reg_t reg = reduct_reg_alloc(compiler);
    compiler->locals[compiler->localCount].name = name;
    compiler->locals[compiler->localCount].expr = REDUCT_EXPR_REG(reg);
    REDUCT_REG_SET_LOCAL(compiler, reg);

    return &compiler->locals[compiler->localCount++];
}

REDUCT_API void reduct_local_pop(reduct_compiler_t* compiler, reduct_uint16_t toCount, reduct_expr_t* result)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    reduct_reg_t resultReg =
        (result != REDUCT_NULL && result->mode == REDUCT_MODE_REG) ? result->reg : REDUCT_REG_INVALID;

    for (reduct_uint32_t i = compiler->localCount; i > (reduct_uint32_t)toCount; i--)
    {
        reduct_local_t* local = &compiler->locals[i - 1];
        if (local->expr.mode == REDUCT_MODE_REG)
        {
            reduct_bool_t isResult = (local->expr.reg == resultReg);
            reduct_bool_t isOuterLocal = REDUCT_FALSE;
            for (reduct_uint32_t j = 0; j < (reduct_uint32_t)toCount; j++)
            {
                if (compiler->locals[j].expr.mode == REDUCT_MODE_REG && compiler->locals[j].expr.reg == local->expr.reg)
                {
                    isOuterLocal = REDUCT_TRUE;
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
        local->name = REDUCT_NULL;
        local->expr = REDUCT_EXPR_NONE();
    }
    compiler->localCount = toCount;
}

REDUCT_API reduct_local_t* reduct_local_lookup(reduct_compiler_t* compiler, reduct_atom_t* name)
{
    REDUCT_ASSERT(compiler != REDUCT_NULL);
    REDUCT_ASSERT(name != REDUCT_NULL);

    for (reduct_int16_t i = compiler->localCount - 1; i >= 0; i--)
    {
        if (compiler->locals[i].name == name)
        {
            return &compiler->locals[i];
        }
    }

    reduct_compiler_t* current = compiler->enclosing;
    while (current != REDUCT_NULL)
    {
        for (reduct_int16_t i = current->localCount - 1; i >= 0; i--)
        {
            if (current->locals[i].name != name)
            {
                continue;
            }

            if (current->locals[i].expr.mode == REDUCT_MODE_CONST)
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

    return REDUCT_NULL;
}