#include "reduct/optimize.h"
#include "reduct/core.h"
#include "reduct/function.h"
#include "reduct/inst.h"
#include "reduct/handle.h"

#include <string.h>

#define REDUCT_OPT_INDEX_NONE ((size_t)-1)

static inline size_t reduct_optimize_skip_nops(reduct_function_t* func, size_t i)
{
    while (i < func->instCount && REDUCT_INST_GET_OP(func->insts[i]) == REDUCT_OPCODE_NOP)
    {
        i++;
    }
    return i;
}

static void reduct_optimize_mark_jump_targets(reduct_function_t* func, bool* targets)
{
    if (func->instCount == 0)
    {
        return;
    }

    memset(targets, 0, func->instCount * sizeof(bool));
    for (size_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t inst = func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(inst);
        if (REDUCT_OPCODE_IS_JUMP(op))
        {
            int32_t offset = (int32_t)REDUCT_INST_GET_SBX(inst);
            size_t targetIdx = i + 1 + offset;
            if (targetIdx < func->instCount)
            {
                targets[targetIdx] = true;
            }
        }
        else if (REDUCT_OPCODE_IS_SKIP(op) && i + 2 < func->instCount)
        {
            targets[i + 2] = true;
        }
    }
}

static bool reduct_optimize_peephole(reduct_function_t* func)
{
    bool changed = false;

    for (size_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);

        if (op == REDUCT_OPCODE_NOP)
        {
            continue;
        }

        if (REDUCT_OPCODE_IS_JUMP(op) && REDUCT_INST_GET_SBX(*inst) == 0)
        {
            *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
            changed = true;
            continue;
        }

        if (REDUCT_OPCODE_IS_SKIP(op))
        {
            size_t nextIdx = reduct_optimize_skip_nops(func, i + 1);
            if (nextIdx < func->instCount)
            {
                reduct_inst_t next = func->insts[nextIdx];
                if (REDUCT_INST_GET_OP(next) == REDUCT_OPCODE_NOP ||
                    (REDUCT_OPCODE_IS_JUMP(REDUCT_INST_GET_OP(next)) && REDUCT_INST_GET_SBX(next) == 0))
                {
                    *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
                    changed = true;
                }
            }
        }

        if (op == REDUCT_OPCODE_MOV && REDUCT_INST_GET_A(*inst) == REDUCT_INST_GET_C(*inst))
        {
            *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
            changed = true;
            continue;
        }

        if (op == REDUCT_OPCODE_JMP)
        {
            int32_t offset = (int32_t)REDUCT_INST_GET_SBX(*inst);
            size_t targetIdx = i + 1 + offset;
            if (targetIdx < func->instCount)
            {
                reduct_inst_t targetInst = func->insts[targetIdx];
                reduct_opcode_t targetOp = REDUCT_INST_GET_OP(targetInst);
                if (targetOp == REDUCT_OPCODE_JMP)
                {
                    int32_t targetOffset = (int32_t)REDUCT_INST_GET_SBX(targetInst);
                    int32_t newOffset = offset + targetOffset + 1;
                    if (newOffset != offset)
                    {
                        *inst = REDUCT_INST_SET_SBX(*inst, newOffset);
                        changed = true;
                    }
                }
                else if (targetOp == REDUCT_OPCODE_RET || targetOp == REDUCT_OPCODE_RET_CONST)
                {
                    *inst = REDUCT_INST_MAKE_ABC(targetOp, 0, 0, REDUCT_INST_GET_C(targetInst));
                    changed = true;
                }
            }
        }

        if (op == REDUCT_OPCODE_CALL || op == REDUCT_OPCODE_CALL_CONST || op == REDUCT_OPCODE_RECUR)
        {
            size_t nextIdx = reduct_optimize_skip_nops(func, i + 1);
            if (nextIdx < func->instCount)
            {
                reduct_inst_t next = func->insts[nextIdx];
                if (REDUCT_INST_GET_OP(next) == REDUCT_OPCODE_RET &&
                    REDUCT_INST_GET_A(*inst) == REDUCT_INST_GET_C(next))
                {
                    if (op == REDUCT_OPCODE_RECUR)
                    {
                        *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_TAILRECUR, REDUCT_INST_GET_A(*inst),
                            REDUCT_INST_GET_B(*inst), 0);
                    }
                    else
                    {
                        reduct_mode_t mode = (reduct_mode_t)(op & REDUCT_MODE_CONST);
                        *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_TAILCALL | mode, REDUCT_INST_GET_A(*inst),
                            REDUCT_INST_GET_B(*inst), REDUCT_INST_GET_C(*inst));
                    }
                    changed = true;
                }
            }
        }

        if (op == REDUCT_OPCODE_MOV || op == REDUCT_OPCODE_MOV_CONST)
        {
            size_t nextIdx = reduct_optimize_skip_nops(func, i + 1);
            if (nextIdx < func->instCount)
            {
                reduct_inst_t next = func->insts[nextIdx];
                if (REDUCT_INST_GET_OP(next) == REDUCT_OPCODE_RET &&
                    REDUCT_INST_GET_A(*inst) == REDUCT_INST_GET_C(next))
                {
                    reduct_mode_t mode = (reduct_mode_t)(op & REDUCT_MODE_CONST);
                    *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RET | mode, 0, 0, REDUCT_INST_GET_C(*inst));
                    changed = true;
                }
            }
        }
    }

    return changed;
}

static bool reduct_optimize_get_constant(reduct_function_t* func, reduct_inst_t inst, reduct_handle_t* out)
{
    reduct_const_slot_t* slot = &func->constants[REDUCT_INST_GET_C(inst)];
    if (slot->type != REDUCT_CONST_SLOT_TYPE_HANDLE)
    {
        return false;
    }

    *out = slot->handle;
    return true;
}

static bool reduct_optimize_constant_fold(reduct_t* reduct, reduct_opcode_t op, reduct_handle_t left,
    reduct_handle_t right, reduct_handle_t* out)
{
    if (!REDUCT_HANDLE_IS_NUMBER_SHAPED(left) || !REDUCT_HANDLE_IS_NUMBER_SHAPED(right))
    {
        return false;
    }

    bool isFloat = REDUCT_HANDLE_IS_FLOAT_SHAPED(left) || REDUCT_HANDLE_IS_FLOAT_SHAPED(right);

    double lf = reduct_handle_as_float(reduct, left);
    double rf = reduct_handle_as_float(reduct, right);
    int64_t li = reduct_handle_as_int(reduct, left);
    int64_t ri = reduct_handle_as_int(reduct, right);

    if (isFloat)
    {
        switch (op)
        {
        case REDUCT_OPCODE_ADD:
        case REDUCT_OPCODE_ADD_CONST:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf + rf);
            return true;
        case REDUCT_OPCODE_SUB:
        case REDUCT_OPCODE_SUB_CONST:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf - rf);
            return true;
        case REDUCT_OPCODE_MUL:
        case REDUCT_OPCODE_MUL_CONST:
            *out = REDUCT_HANDLE_FROM_FLOAT(lf * rf);
            return true;
        case REDUCT_OPCODE_DIV:
        case REDUCT_OPCODE_DIV_CONST:
            if (REDUCT_UNLIKELY(rf == 0.0))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_FLOAT(lf / rf);
            return true;
        default:
            break;
        }
    }
    else
    {
        switch (op)
        {
        case REDUCT_OPCODE_ADD:
        case REDUCT_OPCODE_ADD_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li + ri);
            return true;
        case REDUCT_OPCODE_SUB:
        case REDUCT_OPCODE_SUB_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li - ri);
            return true;
        case REDUCT_OPCODE_MUL:
        case REDUCT_OPCODE_MUL_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li * ri);
            return true;
        case REDUCT_OPCODE_DIV:
        case REDUCT_OPCODE_DIV_CONST:
            if (REDUCT_UNLIKELY(ri == 0))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_INT(li / ri);
            return true;
        case REDUCT_OPCODE_MOD:
        case REDUCT_OPCODE_MOD_CONST:
            if (REDUCT_UNLIKELY(ri == 0))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_INT(li % ri);
            return true;
        case REDUCT_OPCODE_BAND:
        case REDUCT_OPCODE_BAND_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li & ri);
            return true;
        case REDUCT_OPCODE_BOR:
        case REDUCT_OPCODE_BOR_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li | ri);
            return true;
        case REDUCT_OPCODE_BXOR:
        case REDUCT_OPCODE_BXOR_CONST:
            *out = REDUCT_HANDLE_FROM_INT(li ^ ri);
            return true;
        case REDUCT_OPCODE_SHL:
        case REDUCT_OPCODE_SHL_CONST:
            if (REDUCT_UNLIKELY(ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_INT(li << ri);
            return true;
        case REDUCT_OPCODE_SHR:
        case REDUCT_OPCODE_SHR_CONST:
            if (REDUCT_UNLIKELY(ri < 0 || ri >= REDUCT_HANDLE_INT_WIDTH))
            {
                return false;
            }
            *out = REDUCT_HANDLE_FROM_INT(li >> ri);
            return true;
        default:
            break;
        }
    }

    int64_t cmp = reduct_handle_compare(reduct, &left, &right);
    switch (op)
    {
    case REDUCT_OPCODE_EQ:
    case REDUCT_OPCODE_EQ_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp == 0);
        return true;
    case REDUCT_OPCODE_NEQ:
    case REDUCT_OPCODE_NEQ_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp != 0);
        return true;
    case REDUCT_OPCODE_LT:
    case REDUCT_OPCODE_LT_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp < 0);
        return true;
    case REDUCT_OPCODE_LE:
    case REDUCT_OPCODE_LE_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp <= 0);
        return true;
    case REDUCT_OPCODE_GT:
    case REDUCT_OPCODE_GT_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp > 0);
        return true;
    case REDUCT_OPCODE_GE:
    case REDUCT_OPCODE_GE_CONST:
        *out = REDUCT_HANDLE_FROM_BOOL(cmp >= 0);
        return true;
    default:
        break;
    }

    return false;
}

static bool reduct_optimize_constant_folding(reduct_t* reduct, reduct_function_t* func)
{
    bool changed = false;

    REDUCT_SCRATCH(reduct, isJumpTarget, bool, func->instCount);
    reduct_optimize_mark_jump_targets(func, isJumpTarget);

    reduct_inst_t lastInstByReg[REDUCT_REGISTER_MAX];
    for (size_t i = 0; i < REDUCT_REGISTER_MAX; i++)
    {
        lastInstByReg[i] = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
    }

    for (size_t i = 0; i < func->instCount; i++)
    {
        if (isJumpTarget[i])
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                lastInstByReg[r] = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
            }
        }

        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);

        if (op == REDUCT_OPCODE_NOP)
        {
            continue;
        }

        if (REDUCT_OPCODE_HAS_CONST(op) && !(op & REDUCT_MODE_CONST))
        {
            uint64_t c = REDUCT_INST_GET_C(*inst);
            if (c < REDUCT_REGISTER_MAX && REDUCT_INST_GET_OP(lastInstByReg[c]) == REDUCT_OPCODE_MOV_CONST)
            {
                uint8_t constIdx = REDUCT_INST_GET_C(lastInstByReg[c]);
                op = (reduct_opcode_t)(op | REDUCT_MODE_CONST);
                *inst = REDUCT_INST_MAKE_ABC(op, REDUCT_INST_GET_A(*inst), REDUCT_INST_GET_B(*inst), constIdx);
                changed = true;
            }
        }

        if (REDUCT_OPCODE_IS_COMMUTATIVE(op) && !(op & REDUCT_MODE_CONST))
        {
            uint64_t b = REDUCT_INST_GET_B(*inst);
            if (b < REDUCT_REGISTER_MAX && REDUCT_INST_GET_OP(lastInstByReg[b]) == REDUCT_OPCODE_MOV_CONST)
            {
                uint8_t constIdx = REDUCT_INST_GET_C(lastInstByReg[b]);

                op = (reduct_opcode_t)(op | REDUCT_MODE_CONST);
                *inst = REDUCT_INST_MAKE_ABC(op, REDUCT_INST_GET_A(*inst), REDUCT_INST_GET_C(*inst), constIdx);
                changed = true;
            }
        }

        switch (op)
        {
        case REDUCT_OPCODE_ADD_CONST:
        case REDUCT_OPCODE_SUB_CONST:
        case REDUCT_OPCODE_MUL_CONST:
        case REDUCT_OPCODE_DIV_CONST:
        case REDUCT_OPCODE_MOD_CONST:
        case REDUCT_OPCODE_BAND_CONST:
        case REDUCT_OPCODE_BOR_CONST:
        case REDUCT_OPCODE_BXOR_CONST:
        case REDUCT_OPCODE_SHL_CONST:
        case REDUCT_OPCODE_SHR_CONST:
        case REDUCT_OPCODE_EQ_CONST:
        case REDUCT_OPCODE_NEQ_CONST:
        case REDUCT_OPCODE_LT_CONST:
        case REDUCT_OPCODE_LE_CONST:
        case REDUCT_OPCODE_GT_CONST:
        case REDUCT_OPCODE_GE_CONST:
        {
            reduct_reg_t b = REDUCT_INST_GET_B(*inst);
            if (b >= REDUCT_REGISTER_MAX)
            {
                break;
            }

            if (REDUCT_INST_GET_OP(lastInstByReg[b]) != REDUCT_OPCODE_MOV_CONST)
            {
                break;
            }

            reduct_handle_t left;
            reduct_handle_t right;
            if (!reduct_optimize_get_constant(func, lastInstByReg[b], &left) ||
                !reduct_optimize_get_constant(func, *inst, &right))
            {
                break;
            }

            reduct_handle_t result;
            if (!reduct_optimize_constant_fold(reduct, op, left, right, &result))
            {
                break;
            }

            reduct_const_slot_t slot = REDUCT_CONST_SLOT_HANDLE(result);
            reduct_const_t constIdx = reduct_function_lookup_constant(reduct, func, &slot);

            *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV_CONST, REDUCT_INST_GET_A(*inst), 0, constIdx);
            changed = true;
        }
        break;
        default:
            break;
        }

        op = REDUCT_INST_GET_OP(*inst);
        if (REDUCT_OPCODE_HAS_TARGET(op))
        {
            reduct_reg_t target = REDUCT_INST_GET_A(*inst);
            if (target < REDUCT_REGISTER_MAX)
            {
                lastInstByReg[target] = *inst;
            }
        }

        if (REDUCT_OPCODE_IS_CALL(op))
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                lastInstByReg[r] = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
            }
        }
    }

    REDUCT_SCRATCH_FREE(reduct, isJumpTarget);
    return changed;
}

static bool reduct_optimize_dead_store(reduct_t* reduct, reduct_function_t* func)
{
    bool changed = false;

    REDUCT_SCRATCH(reduct, isJumpTarget, bool, func->instCount);
    reduct_optimize_mark_jump_targets(func, isJumpTarget);

    size_t lastWrite[REDUCT_REGISTER_MAX];
    for (size_t i = 0; i < REDUCT_REGISTER_MAX; i++)
    {
        lastWrite[i] = REDUCT_OPT_INDEX_NONE;
    }

    for (size_t i = 0; i < func->instCount; i++)
    {
        if (isJumpTarget[i])
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                lastWrite[r] = REDUCT_OPT_INDEX_NONE;
            }
        }

        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);

        if (op == REDUCT_OPCODE_NOP)
        {
            continue;
        }

        if (REDUCT_OPCODE_READS_A(op))
        {
            if (REDUCT_OPCODE_READS_RANGE(op))
            {
                uint8_t a = REDUCT_INST_GET_A(*inst);
                uint8_t count = REDUCT_INST_GET_B(*inst);
                for (size_t r = 0; r < count && (a + r) < REDUCT_REGISTER_MAX; r++)
                {
                    lastWrite[a + r] = REDUCT_OPT_INDEX_NONE;
                }
            }
            else
            {
                lastWrite[REDUCT_INST_GET_A(*inst)] = REDUCT_OPT_INDEX_NONE;
            }
        }
        if (REDUCT_OPCODE_READS_B(op))
        {
            lastWrite[REDUCT_INST_GET_B(*inst)] = REDUCT_OPT_INDEX_NONE;
        }

        if (REDUCT_OPCODE_READS_C(op) && !(op & REDUCT_MODE_CONST))
        {
            lastWrite[REDUCT_INST_GET_C(*inst)] = REDUCT_OPT_INDEX_NONE;
        }

        if (REDUCT_OPCODE_HAS_TARGET(op) && !REDUCT_OPCODE_READS_RANGE(op))
        {
            uint8_t target = REDUCT_INST_GET_A(*inst);
            if (lastWrite[target] != REDUCT_OPT_INDEX_NONE)
            {
                func->insts[lastWrite[target]] = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
                changed = true;
            }
            lastWrite[target] = i;
        }

        if (REDUCT_OPCODE_IS_CALL(op))
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                lastWrite[r] = REDUCT_OPT_INDEX_NONE;
            }
        }
    }

    REDUCT_SCRATCH_FREE(reduct, isJumpTarget);
    return changed;
}

static bool reduct_optimize_mov_propagate(reduct_t* reduct, reduct_function_t* func)
{
    bool changed = false;

    REDUCT_SCRATCH(reduct, isJumpTarget, bool, func->instCount);
    reduct_optimize_mark_jump_targets(func, isJumpTarget);

    reduct_reg_t mappedReg[REDUCT_REGISTER_MAX];
    bool isConstMap[REDUCT_REGISTER_MAX];
    for (size_t i = 0; i < REDUCT_REGISTER_MAX; i++)
    {
        mappedReg[i] = REDUCT_REG_INVALID;
    }

    for (size_t i = 0; i < func->instCount; i++)
    {
        if (isJumpTarget[i])
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                mappedReg[r] = REDUCT_REG_INVALID;
            }
        }

        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);
        if (op == REDUCT_OPCODE_NOP)
            continue;

        if (REDUCT_OPCODE_READS_B(op))
        {
            uint8_t b = REDUCT_INST_GET_B(*inst);
            if (mappedReg[b] != REDUCT_REG_INVALID && !isConstMap[b])
            {
                *inst = REDUCT_INST_MAKE_ABC(op, REDUCT_INST_GET_A(*inst), mappedReg[b], REDUCT_INST_GET_C(*inst));
                changed = true;
            }
        }

        if (REDUCT_OPCODE_READS_C(op) && !(op & REDUCT_MODE_CONST))
        {
            uint8_t c = REDUCT_INST_GET_C(*inst);
            if (mappedReg[c] != REDUCT_REG_INVALID)
            {
                if (isConstMap[c] && REDUCT_OPCODE_HAS_CONST(op))
                {
                    reduct_opcode_t constOp = (reduct_opcode_t)(op | REDUCT_MODE_CONST);
                    *inst =
                        REDUCT_INST_MAKE_ABC(constOp, REDUCT_INST_GET_A(*inst), REDUCT_INST_GET_B(*inst), mappedReg[c]);
                    changed = true;
                }
                else if (!isConstMap[c])
                {
                    *inst = REDUCT_INST_MAKE_ABC(op, REDUCT_INST_GET_A(*inst), REDUCT_INST_GET_B(*inst), mappedReg[c]);
                    changed = true;
                }
            }
        }

        if (REDUCT_OPCODE_HAS_TARGET(op))
        {
            uint8_t target = REDUCT_INST_GET_A(*inst);
            mappedReg[target] = REDUCT_REG_INVALID;

            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                if (mappedReg[r] == target && !isConstMap[r])
                {
                    mappedReg[r] = REDUCT_REG_INVALID;
                }
            }
        }

        if (op == REDUCT_OPCODE_MOV || op == REDUCT_OPCODE_MOV_CONST)
        {
            uint8_t target = REDUCT_INST_GET_A(*inst);
            mappedReg[target] = REDUCT_INST_GET_C(*inst);
            isConstMap[target] = (op == REDUCT_OPCODE_MOV_CONST);
        }

        if (REDUCT_OPCODE_IS_CALL(op))
        {
            for (size_t r = 0; r < REDUCT_REGISTER_MAX; r++)
            {
                mappedReg[r] = REDUCT_REG_INVALID;
            }
        }
    }

    REDUCT_SCRATCH_FREE(reduct, isJumpTarget);
    return changed;
}

static bool reduct_optimize_dead_code(reduct_t* reduct, reduct_function_t* func)
{
    bool changed = false;

    REDUCT_SCRATCH(reduct, isJumpTarget, bool, func->instCount);
    reduct_optimize_mark_jump_targets(func, isJumpTarget);

    bool reachable = true;
    for (size_t i = 0; i < func->instCount; i++)
    {
        if (isJumpTarget[i])
        {
            reachable = true;
        }

        reduct_opcode_t op = REDUCT_INST_GET_OP(func->insts[i]);

        if (!reachable && op != REDUCT_OPCODE_NOP)
        {
            func->insts[i] = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0);
            changed = true;
            continue;
        }

        if (REDUCT_OPCODE_IS_TERMINATOR(op))
        {
            reachable = false;
        }
    }

    REDUCT_SCRATCH_FREE(reduct, isJumpTarget);
    return changed;
}

static bool reduct_optimize_algebraic(reduct_t* reduct, reduct_function_t* func)
{
    bool changed = false;

    for (size_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);

        if (!(op & REDUCT_MODE_CONST))
        {
            continue;
        }

        reduct_handle_t constVal;
        if (!reduct_optimize_get_constant(func, *inst, &constVal))
        {
            continue;
        }

        // Probably does not matter if we are checking by int or float, will be the same either way.
        if (REDUCT_HANDLE_IS_NUMBER_SHAPED(constVal))
        {
            double val = reduct_handle_as_float(reduct, constVal);
            uint8_t a = REDUCT_INST_GET_A(*inst);
            uint8_t b = REDUCT_INST_GET_B(*inst);

            if ((op == REDUCT_OPCODE_ADD_CONST || op == REDUCT_OPCODE_SUB_CONST) && val == 0.0)
            {
                *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV, a, 0, b);
                changed = true;
            }
            else if ((op == REDUCT_OPCODE_MUL_CONST || op == REDUCT_OPCODE_DIV_CONST) && val == 1.0)
            {
                *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV, a, 0, b);
                changed = true;
            }
            else if (op == REDUCT_OPCODE_MUL_CONST && val == 0.0)
            {
                *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV_CONST, a, 0, REDUCT_INST_GET_C(*inst));
                changed = true;
            }
            else if (op == REDUCT_OPCODE_MUL_CONST && val == 2.0)
            {
                *inst = REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_ADD, a, b, b);
                changed = true;
            }
            /// @todo Could probably add more algebraic simplifications.
        }
    }
    return changed;
}

static void reduct_optimize_compact_constants(reduct_t* reduct, reduct_function_t* func)
{
    if (func->constantCount == 0)
    {
        return;
    }

    REDUCT_SCRATCH(reduct, used, bool, func->constantCount);
    memset(used, 0, func->constantCount * sizeof(bool));

    for (uint16_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
        {
            used[i] = true;
        }
    }

    for (uint32_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t inst = func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(inst);
        bool isConstIdx = (op == REDUCT_OPCODE_CLOSURE) || (REDUCT_OPCODE_HAS_CONST(op) && (op & REDUCT_MODE_CONST));

        if (isConstIdx)
        {
            uint32_t c = REDUCT_INST_GET_C(inst);
            if (c < func->constantCount)
            {
                used[c] = true;
            }
        }
    }

    REDUCT_SCRATCH(reduct, mapping, uint16_t, func->constantCount);
    for (uint16_t i = 0; i < func->constantCount; i++)
    {
        mapping[i] = i;
    }

    uint16_t writePtr = 0;
    for (uint16_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
            continue;

        if (used[i] && func->constants[i].type == REDUCT_CONST_SLOT_TYPE_HANDLE)
        {
            while (writePtr < i && func->constants[writePtr].type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
            {
                writePtr++;
            }

            if (writePtr < i)
            {
                func->constants[writePtr] = func->constants[i];
                mapping[i] = writePtr;
                func->constants[i].type = REDUCT_CONST_SLOT_TYPE_NONE;
                writePtr++;
            }
            else
            {
                writePtr = i + 1;
            }
        }
        else
        {
            func->constants[i].type = REDUCT_CONST_SLOT_TYPE_NONE;
        }
    }

    for (uint32_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);
        if (op == REDUCT_OPCODE_CLOSURE || (REDUCT_OPCODE_HAS_CONST(op) && (op & REDUCT_MODE_CONST)))
        {
            *inst = REDUCT_INST_SET_C(*inst, mapping[REDUCT_INST_GET_C(*inst)]);
        }
    }

    while (func->constantCount > 0 && func->constants[func->constantCount - 1].type == REDUCT_CONST_SLOT_TYPE_NONE)
    {
        func->constantCount--;
    }

    REDUCT_SCRATCH_FREE(reduct, mapping);
    REDUCT_SCRATCH_FREE(reduct, used);
}

static void reduct_optimize_compact(reduct_t* reduct, reduct_function_t* func)
{
    size_t oldCount = func->instCount;
    size_t writeIdx = 0;

    REDUCT_SCRATCH(reduct, oldToNew, size_t, oldCount);
    REDUCT_SCRATCH(reduct, newToOld, size_t, oldCount);

    for (size_t readIdx = 0; readIdx < oldCount; readIdx++)
    {
        if (REDUCT_INST_GET_OP(func->insts[readIdx]) == REDUCT_OPCODE_NOP)
        {
            oldToNew[readIdx] = REDUCT_OPT_INDEX_NONE;
            continue;
        }

        oldToNew[readIdx] = writeIdx;
        newToOld[writeIdx] = readIdx;
        if (readIdx != writeIdx)
        {
            func->insts[writeIdx] = func->insts[readIdx];
            if (func->positions)
            {
                func->positions[writeIdx] = func->positions[readIdx];
            }
        }
        writeIdx++;
    }

    func->instCount = writeIdx;

    for (size_t i = 0; i < func->instCount; i++)
    {
        reduct_inst_t* inst = &func->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(*inst);
        size_t oldSource = newToOld[i];

        if (REDUCT_OPCODE_IS_JUMP(op))
        {
            int32_t offset = (int32_t)REDUCT_INST_GET_SBX(*inst);
            size_t oldTarget = oldSource + 1 + offset;
            int32_t newOffset;

            if (oldTarget >= oldCount)
            {
                newOffset = (int32_t)(func->instCount - i - 1);
            }
            else
            {
                while (oldTarget < oldCount && oldToNew[oldTarget] == REDUCT_OPT_INDEX_NONE)
                {
                    oldTarget++;
                }

                if (oldTarget >= oldCount)
                {
                    newOffset = (int32_t)(func->instCount - i - 1);
                }
                else
                {
                    newOffset = (int32_t)(oldToNew[oldTarget] - i - 1);
                }
            }
            *inst = REDUCT_INST_SET_SBX(*inst, newOffset);
        }
    }

    REDUCT_SCRATCH_FREE(reduct, oldToNew);
    REDUCT_SCRATCH_FREE(reduct, newToOld);

    reduct_optimize_compact_constants(reduct, func);
}

REDUCT_API void reduct_optimize(reduct_t* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        return;
    }

    reduct_function_t* func = REDUCT_HANDLE_TO_FUNCTION(handle);
    if (func->instCount == 0)
    {
        return;
    }

    if (func->flags & REDUCT_FUNCTION_FLAG_OPTIMIZED)
    {
        return;
    }
    func->flags |= REDUCT_FUNCTION_FLAG_OPTIMIZED;

    bool changed = true;
    while (changed)
    {
        changed = false;

        bool passChanged = true;
        while (flags & REDUCT_OPTIMIZE_PEEPHOLE && passChanged)
        {
            passChanged = reduct_optimize_peephole(func);
            if (passChanged)
            {
                changed = true;
            }
        }

        if (flags & REDUCT_OPTIMIZE_CONSTANT_FOLDING && reduct_optimize_constant_folding(reduct, func))
        {
            changed = true;
        }
        if (flags & REDUCT_OPTIMIZE_DEAD_STORE && reduct_optimize_dead_store(reduct, func))
        {
            changed = true;
        }
        if (flags & REDUCT_OPTIMIZE_MOV_PROPAGATE && reduct_optimize_mov_propagate(reduct, func))
        {
            changed = true;
        }
        if (flags & REDUCT_OPTIMIZE_DEAD_CODE && reduct_optimize_dead_code(reduct, func))
        {
            changed = true;
        }
        if (flags & REDUCT_OPTIMIZE_ALGEBRAIC && reduct_optimize_algebraic(reduct, func))
        {
            changed = true;
        }

        reduct_optimize_compact(reduct, func);
    }

    for (size_t i = 0; i < func->constantCount; i++)
    {
        reduct_const_slot_t* slot = &func->constants[i];
        if (slot->type == REDUCT_CONST_SLOT_TYPE_HANDLE && REDUCT_HANDLE_IS_FUNCTION(slot->handle))
        {
            reduct_optimize(reduct, slot->handle, flags);
        }
    }

    func->optimizeFlags = flags;
}
