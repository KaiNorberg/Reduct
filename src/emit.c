#include "reduct/module.h"
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/emit.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/opcode.h>
#include <reduct/rvsdg.h>
#include <reduct/scratch.h>

#include <stdint.h>
#include <stdlib.h>

#define REDUCT_REGISTER_RETURN ((reduct_reg_t)0xFFFE)

typedef enum
{
    REDUCT_EMITTER_EXPR_TYPE_NONE,
    REDUCT_EMITTER_EXPR_TYPE_REG,
    REDUCT_EMITTER_EXPR_TYPE_REG_FORK,
    REDUCT_EMITTER_EXPR_TYPE_CONST,
} reduct_emitter_expr_type_t;

typedef struct reduct_emitter_expr
{
    reduct_emitter_expr_type_t type;
    struct reduct_rvsdg_origin* origin;
    union {
        reduct_reg_t reg;
        reduct_const_t constant;
        uint16_t raw;
    };
} reduct_emitter_expr_t;

#define REDUCT_EMITTER_EXPR_REG(_reg) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_REG, .origin = NULL, .reg = (_reg)})

#define REDUCT_EMITTER_EXPR_REG_FORK(_reg) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_REG_FORK, .origin = NULL, .reg = (_reg)})

#define REDUCT_EMITTER_EXPR_CONST(_const) \
    ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_CONST, .origin = NULL, .constant = (_const)})

#define REDUCT_EMITTER_EXPR_NONE ((reduct_emitter_expr_t){.type = REDUCT_EMITTER_EXPR_TYPE_NONE, .origin = NULL})

typedef struct reduct_emitter_cache_entry
{
    struct reduct_rvsdg_origin* origin;
    reduct_emitter_expr_t expr;
    uint16_t remainingUses;
} reduct_emitter_cache_entry_t;

typedef struct reduct_emitter
{
    struct reduct* reduct;
    reduct_function_t* function;
    reduct_reg_t topReg;
    reduct_reg_t freeRegs[REDUCT_REGISTER_MAX];
    uint32_t freeCount;
    struct reduct_rvsdg_node* phiNode;
    reduct_emitter_cache_entry_t cache[REDUCT_REGISTER_MAX];
    size_t cacheCount;
    struct reduct_item* lastItem;
} reduct_emitter_t;

static void reduct_emitter_init(reduct_emitter_t* emitter, reduct_t* reduct, reduct_function_t* function,
    reduct_rvsdg_node_t* node)
{
    assert(emitter != NULL);
    assert(reduct != NULL);
    assert(function != NULL);
    assert(node != NULL);

    emitter->reduct = reduct;
    emitter->function = function;
    if (node->parent != NULL && node->parent->parent != NULL &&
        node->parent->parent->type == REDUCT_RVSDG_NODE_TYPE_PHI)
    {
        emitter->phiNode = node->parent->parent;
    }
    emitter->lastItem = NULL;
}

static void reduct_emitter_deinit(reduct_emitter_t* emitter)
{
    assert(emitter != NULL);
    REDUCT_UNUSED(emitter);
}

static reduct_reg_t reduct_emitter_reg_alloc(reduct_emitter_t* emitter)
{
    assert(emitter != NULL);

    if (emitter->freeCount > 0)
    {
        reduct_reg_t reg = emitter->freeRegs[--emitter->freeCount];
        assert(reg < REDUCT_REGISTER_MAX);
        return reg;
    }

    if (emitter->topReg >= REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_THROW(emitter->reduct, "register overflow (spilling not implemented)");
    }

    reduct_reg_t reg = emitter->topReg++;
    assert(reg < REDUCT_REGISTER_MAX);
    if (reg >= emitter->function->registerCount)
    {
        emitter->function->registerCount = reg + 1;
    }
    return reg;
}

static void reduct_emitter_reg_free(reduct_emitter_t* emitter, reduct_reg_t reg)
{
    assert(emitter != NULL);
    assert(reg < REDUCT_REGISTER_MAX);

    for (uint16_t i = 0; i < emitter->freeCount; i++)
    {
        if (emitter->freeRegs[i] == reg)
        {
            return;
        }
    }

    if (reg == emitter->topReg - 1)
    {
        emitter->topReg--;

        while (true)
        {
            bool found = false;
            for (uint16_t i = 0; i < emitter->freeCount; i++)
            {
                if (emitter->freeRegs[i] == emitter->topReg - 1)
                {
                    found = true;
                    for (uint16_t j = i; j < emitter->freeCount - 1; j++)
                    {
                        emitter->freeRegs[j] = emitter->freeRegs[j + 1];
                    }
                    emitter->freeCount--;
                    emitter->topReg--;
                    break;
                }
            }
            if (!found)
            {
                break;
            }
        }

        return;
    }

    emitter->freeRegs[emitter->freeCount++] = reg;
}

static reduct_reg_t reduct_emitter_reg_alloc_range(reduct_emitter_t* emitter, uint16_t count)
{
    uint16_t allocCount = count > 0 ? count : 1;
    if (emitter->topReg + allocCount > REDUCT_REGISTER_MAX)
    {
        REDUCT_ERROR_THROW(emitter->reduct, "register overflow (spilling not implemented)");
    }
    reduct_reg_t startReg = emitter->topReg;
    emitter->topReg += allocCount;
    if (startReg + allocCount > emitter->function->registerCount)
    {
        emitter->function->registerCount = startReg + allocCount;
    }
    return startReg;
}

static reduct_emitter_expr_t reduct_emitter_cache_get(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin)
{
    if (origin == NULL || emitter->cacheCount == 0)
    {
        return REDUCT_EMITTER_EXPR_NONE;
    }

    for (size_t i = 0; i < emitter->cacheCount; i++)
    {
        if (emitter->cache[i].origin == origin)
        {
            return emitter->cache[i].expr;
        }
    }
    return REDUCT_EMITTER_EXPR_NONE;
}

static void reduct_emitter_cache_put(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_emitter_expr_t expr)
{
    assert(origin != NULL);

    for (size_t i = 0; i < emitter->cacheCount; i++)
    {
        if (emitter->cache[i].remainingUses == 0)
        {
            emitter->cache[i].origin = origin;
            emitter->cache[i].expr = expr;
            emitter->cache[i].remainingUses = origin->useCount;
            return;
        }
    }

    if (REDUCT_UNLIKELY(emitter->cacheCount >= REDUCT_REGISTER_MAX))
    {
        REDUCT_ERROR_THROW(emitter->reduct, "emitter cache overflow");
    }
    emitter->cache[emitter->cacheCount].origin = origin;
    emitter->cache[emitter->cacheCount].expr = expr;
    emitter->cache[emitter->cacheCount].remainingUses = origin->useCount;
    emitter->cacheCount++;
}

static void reduct_emitter_cache_update(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_emitter_expr_t expr)
{
    if (origin == NULL || emitter->cacheCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < emitter->cacheCount; i++)
    {
        if (emitter->cache[i].origin == origin)
        {
            emitter->cache[i].expr = expr;
            return;
        }
    }
}

static void reduct_emitter_cache_release(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin)
{
    if (origin == NULL || emitter->cacheCount == 0)
    {
        return;
    }

    for (size_t i = 0; i < emitter->cacheCount; i++)
    {
        reduct_emitter_cache_entry_t* entry = &emitter->cache[i];

        if (entry->origin != origin)
        {
            continue;
        }

        if (entry->remainingUses == 0)
        {
            return;
        }

        entry->remainingUses--;
        if (entry->remainingUses > 0)
        {
            return;
        }

        if (entry->expr.type != REDUCT_EMITTER_EXPR_TYPE_REG && entry->expr.type != REDUCT_EMITTER_EXPR_TYPE_REG_FORK)
        {
            return;
        }

        reduct_emitter_reg_free(emitter, entry->expr.reg);
        entry->origin = NULL;
        entry->expr = REDUCT_EMITTER_EXPR_NONE;
    }
}

static void reduct_emit_inst(reduct_emitter_t* emitter, reduct_inst_t inst);
static reduct_emitter_expr_t reduct_emit_origin(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_reg_t target);
static reduct_function_t* reduct_emit_function(reduct_t* reduct, reduct_rvsdg_node_t* lambda);

static reduct_reg_t reduct_emitter_expr_flush(reduct_emitter_t* emitter, reduct_emitter_expr_t* expr,
    reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(expr != NULL);

    if (expr->type == REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        return REDUCT_REGISTER_INVALID;
    }

    if (expr->type == REDUCT_EMITTER_EXPR_TYPE_REG_FORK)
    {
        reduct_reg_t joinTarget =
            (target == REDUCT_REGISTER_RETURN || target == REDUCT_REGISTER_INVALID) ? expr->reg : target;
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_JOIN, joinTarget, 0, expr->reg));
        if (target == REDUCT_REGISTER_RETURN)
        {
            reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RET, 0, 0, joinTarget));
            return REDUCT_REGISTER_INVALID;
        }
        return joinTarget;
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        reduct_opcode_t op = REDUCT_OPCODE_RET;
        uint16_t operand = expr->reg;
        if (expr->type == REDUCT_EMITTER_EXPR_TYPE_CONST)
        {
            op |= REDUCT_OPCODE_MODE_CONST;
            operand = expr->constant;
        }
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(op, 0, 0, operand));
        return REDUCT_REGISTER_INVALID;
    }

    reduct_reg_t destReg = target;
    if (destReg == REDUCT_REGISTER_INVALID)
    {
        if (expr->type == REDUCT_EMITTER_EXPR_TYPE_REG)
        {
            return expr->reg;
        }
        destReg = reduct_emitter_reg_alloc(emitter);
    }

    if (expr->type == REDUCT_EMITTER_EXPR_TYPE_CONST)
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV_CONST, destReg, 0, expr->constant));
    }
    else if (expr->reg != destReg)
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV, destReg, 0, expr->reg));
    }

    if (expr->origin != NULL && target == REDUCT_REGISTER_INVALID)
    {
        reduct_emitter_cache_update(emitter, expr->origin, REDUCT_EMITTER_EXPR_REG(destReg));
    }

    return destReg;
}

static void reduct_emit_inst(reduct_emitter_t* emitter, reduct_inst_t inst)
{
    assert(emitter != NULL);
    uint32_t modulePos = emitter->lastItem != NULL ? emitter->lastItem->modulePos : 0;
    reduct_module_id_t moduleId = emitter->lastItem != NULL ? emitter->lastItem->moduleId : REDUCT_MODULE_ID_NONE;
    reduct_function_emit(emitter->reduct, emitter->function, inst, modulePos, moduleId);
}

static void reduct_emit_inst_abc(reduct_emitter_t* emitter, reduct_opcode_t opcode, uint32_t a, uint32_t b,
    reduct_emitter_expr_t* c)
{
    assert(emitter != NULL);
    assert(c != NULL);
    assert(a < REDUCT_REGISTER_MAX);
    assert(b < REDUCT_REGISTER_MAX);
    assert(c->type != REDUCT_EMITTER_EXPR_TYPE_REG || c->reg < REDUCT_REGISTER_MAX);


    reduct_inst_t inst;
    if (c->type == REDUCT_EMITTER_EXPR_TYPE_CONST && REDUCT_OPCODE_HAS_CONST(opcode))
    {
        inst = REDUCT_INST_MAKE_ABC(opcode | REDUCT_OPCODE_MODE_CONST, a, b, c->constant);
    }
    else
    {
        reduct_emitter_expr_flush(emitter, c, REDUCT_REGISTER_INVALID);
        inst = REDUCT_INST_MAKE_ABC(opcode, a, b, c->reg);
    }
    reduct_emit_inst(emitter, inst);
}

static reduct_emitter_expr_t reduct_emit_simple_opcode_abc(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    assert(node->firstInput != NULL);
    assert(node->firstInput->next != NULL);
    assert(node->firstInput->edge != NULL);
    assert(node->firstInput->next->edge != NULL);

    reduct_rvsdg_origin_t* inputB = node->firstInput->edge->origin;
    reduct_rvsdg_origin_t* inputC = node->firstInput->next->edge->origin;

    reduct_emitter_expr_t exprB = reduct_emit_origin(emitter, inputB, REDUCT_REGISTER_INVALID);
    reduct_emitter_expr_t exprC = reduct_emit_origin(emitter, inputC, REDUCT_REGISTER_INVALID);

    reduct_reg_t regB = reduct_emitter_expr_flush(emitter, &exprB, REDUCT_REGISTER_INVALID);

    reduct_emitter_cache_release(emitter, inputB);
    reduct_emitter_cache_release(emitter, inputC);

    if (target >= REDUCT_REGISTER_MAX)
    {
        target = reduct_emitter_reg_alloc(emitter);
    }

    reduct_emit_inst_abc(emitter, node->opcode, target, regB, &exprC);

    return REDUCT_EMITTER_EXPR_REG(target);
}

static reduct_emitter_expr_t reduct_emit_simple_opcode_ac(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    assert(node->firstInput != NULL);
    assert(node->firstInput->edge != NULL);
    assert(node->firstInput->next == NULL);

    reduct_rvsdg_origin_t* inputC = node->firstInput->edge->origin;
    reduct_emitter_expr_t exprC = reduct_emit_origin(emitter, inputC, REDUCT_REGISTER_INVALID);

    reduct_emitter_cache_release(emitter, inputC);

    if (target >= REDUCT_REGISTER_MAX)
    {
        target = reduct_emitter_reg_alloc(emitter);
    }

    reduct_emit_inst_abc(emitter, node->opcode, target, 0, &exprC);

    return REDUCT_EMITTER_EXPR_REG(target);
}

static bool reduct_emitter_origin_is_phi_recur(reduct_emitter_t* emitter, const reduct_rvsdg_origin_t* origin)
{
    if (emitter->phiNode == NULL)
    {
        return false;
    }

    while (origin != NULL)
    {
        if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE && origin->node == emitter->phiNode)
        {
            return true;
        }

        if (reduct_rvsdg_node_is_recur_origin(emitter->phiNode, (reduct_rvsdg_origin_t*)origin))
        {
            return true;
        }

        if (origin->ownerKind == REDUCT_RVSDG_OWNER_REGION)
        {
            reduct_rvsdg_region_t* region = origin->region;
            reduct_rvsdg_node_t* parent = (region != NULL) ? region->parent : NULL;

            uint16_t moduleIdx;
            if (parent != NULL && reduct_rvsdg_node_argument_to_input(region, origin->index, &moduleIdx))
            {
                reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(parent, moduleIdx);
                if (input == NULL || input->edge == NULL)
                {
                    return false;
                }
                origin = input->edge->origin;
                continue;
            }
        }

        break;
    }

    return false;
}

static reduct_emitter_expr_t reduct_emit_simple_opcode_abc_range(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    assert(node->firstInput != NULL);
    assert(node->firstInput->edge != NULL);

    bool isRecur = false;
    reduct_opcode_t op = node->opcode;
    if (REDUCT_OPCODE_IS_CALL(op) && !REDUCT_OPCODE_IS_RECUR(op) && !REDUCT_OPCODE_IS_FORK(op))
    {
        reduct_rvsdg_user_t* callableInput = reduct_rvsdg_node_get_input(node, 0);
        if (callableInput != NULL && callableInput->edge != NULL &&
            reduct_emitter_origin_is_phi_recur(emitter, callableInput->edge->origin))
        {
            isRecur = true;
            op = REDUCT_OPCODE_TO_RECUR(op);
            assert(REDUCT_OPCODE_GET_LAYOUT(op) == REDUCT_OPCODE_LAYOUT_AB_RANGE);
        }
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        op = REDUCT_OPCODE_TO_TAIL(op);
    }

    assert(node->inputCount > 0);
    uint32_t arity = node->inputCount - 1;
    reduct_emitter_expr_t args[UINT8_MAX];

    reduct_emitter_expr_t exprC = REDUCT_EMITTER_EXPR_NONE;
    reduct_rvsdg_origin_t* inputC = node->firstInput->edge->origin;
    if (!isRecur)
    {
        reduct_rvsdg_origin_t* inputC = node->firstInput->edge->origin;
        exprC = reduct_emit_origin(emitter, inputC, REDUCT_REGISTER_INVALID);
    }

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        if (input->index == 0)
        {
            input = input->next;
            continue;
        }

        assert(input->edge != NULL);
        args[input->index - 1] = reduct_emit_origin(emitter, input->edge->origin, REDUCT_REGISTER_INVALID);
        input = input->next;
    }

    reduct_reg_t base = reduct_emitter_reg_alloc_range(emitter, arity);
    for (int32_t i = arity - 1; i >= 0; i--)
    {
        reduct_emitter_expr_flush(emitter, &args[i], (reduct_reg_t)(base + i));
    }

    input = node->firstInput;
    while (input != NULL)
    {
        if (input->index == 0)
        {
            input = input->next;
            continue;
        }

        assert(input->edge != NULL);
        reduct_emitter_cache_release(emitter, input->edge->origin);
        input = input->next;
    }

    if (isRecur)
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(op, base, arity, 0));
    }
    else
    {
        reduct_emit_inst_abc(emitter, op, base, arity, &exprC);
        reduct_emitter_cache_release(emitter, inputC);
    }

    for (uint32_t i = 1; i < arity; i++)
    {
        reduct_emitter_reg_free(emitter, (reduct_reg_t)(base + i));
    }

    if (target == REDUCT_REGISTER_RETURN && REDUCT_OPCODE_IS_TERMINATOR(op))
    {
        reduct_emitter_reg_free(emitter, base);
        return REDUCT_EMITTER_EXPR_NONE;
    }

    if (REDUCT_OPCODE_IS_FORK(op))
    {
        return REDUCT_EMITTER_EXPR_REG_FORK(base);
    }

    return REDUCT_EMITTER_EXPR_REG(base);
}

static reduct_emitter_expr_t reduct_emit_simple_opcode_ab_range(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    uint32_t arity = node->inputCount;
    reduct_emitter_expr_t args[UINT8_MAX];

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        assert(input->edge != NULL);
        args[input->index] = reduct_emit_origin(emitter, input->edge->origin, REDUCT_REGISTER_INVALID);
        input = input->next;
    }

    input = node->firstInput;
    while (input != NULL)
    {
        assert(input->edge != NULL);
        reduct_emitter_cache_release(emitter, input->edge->origin);
        input = input->next;
    }

    reduct_reg_t base = reduct_emitter_reg_alloc_range(emitter, arity);
    for (int32_t i = arity - 1; i >= 0; i--)
    {
        reduct_emitter_expr_flush(emitter, &args[i], (reduct_reg_t)(base + i));
    }

    reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(node->opcode, base, arity, 0));

    for (uint32_t i = 1; i < arity; i++)
    {
        reduct_emitter_reg_free(emitter, (reduct_reg_t)(base + i));
    }

    return REDUCT_EMITTER_EXPR_REG(base);
}

static reduct_emitter_expr_t reduct_emit_simple_opcode(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    reduct_opcode_t op = node->opcode;

    reduct_opcode_layout_t layout = REDUCT_OPCODE_GET_LAYOUT(op);

    switch (layout)
    {
    case REDUCT_OPCODE_LAYOUT_ABC:
    {
        return reduct_emit_simple_opcode_abc(emitter, node, target);
    }
    break;
    case REDUCT_OPCODE_LAYOUT_AC:
    {
        return reduct_emit_simple_opcode_ac(emitter, node, target);
    }
    break;
    case REDUCT_OPCODE_LAYOUT_ABC_RANGE:
    {
        return reduct_emit_simple_opcode_abc_range(emitter, node, target);
    }
    break;
    case REDUCT_OPCODE_LAYOUT_AB_RANGE:
    {
        return reduct_emit_simple_opcode_ab_range(emitter, node, target);
    }
    break;
    default:
        REDUCT_ERROR_THROW(emitter->reduct, "unsupported opcode %d", op);
    }
}

static reduct_emitter_expr_t reduct_emit_simple_constant(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    reduct_const_t constant = reduct_function_add_constant(emitter->reduct, emitter->function, node->constant);
    return REDUCT_EMITTER_EXPR_CONST(constant);
}

static reduct_emitter_expr_t reduct_emit_lambda(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    assert(emitter != NULL && node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA);

    reduct_reg_t target = reduct_emitter_reg_alloc(emitter);
    reduct_emitter_expr_t result = REDUCT_EMITTER_EXPR_REG(target);
    reduct_emitter_cache_put(emitter, node->output, result);

    reduct_function_t* function = reduct_emit_function(emitter->reduct, node);
    reduct_const_t constant =
        reduct_function_add_constant(emitter->reduct, emitter->function, REDUCT_HANDLE_FROM_FUNCTION(function));

    reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_CLOSURE_CONST, target, 0, constant));

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        if (input->edge == NULL)
        {
            input = input->next;
            continue;
        }

        reduct_rvsdg_origin_t* origin = input->edge->origin;
        reduct_emitter_expr_t valExpr = reduct_emit_origin(emitter, origin, REDUCT_REGISTER_INVALID);
        reduct_emit_inst_abc(emitter, REDUCT_OPCODE_CAPTURE, target, input->index, &valExpr);
        reduct_emitter_cache_release(emitter, origin);
        input = input->next;
    }

    return result;
}

static reduct_emitter_expr_t reduct_emit_phi(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    assert(emitter != NULL && node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_PHI);
    return reduct_emit_origin(emitter, node->firstRegion->result->edge->origin, REDUCT_REGISTER_INVALID);
}

static void reduct_emitter_patch_jmp_here(reduct_emitter_t* emitter, uint32_t jmpIdx)
{
    int32_t offset = (int32_t)emitter->function->instCount - (int32_t)jmpIdx - 1;
    if (offset < INT16_MIN || offset > INT16_MAX)
    {
        REDUCT_ERROR_THROW(emitter->reduct, "gamma jump offset out of range");
    }
    emitter->function->insts[jmpIdx] = REDUCT_INST_SET_SAX(emitter->function->insts[jmpIdx], (int16_t)offset);
}

static reduct_emitter_expr_t reduct_emit_gamma(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_GAMMA);
    assert(node->regionCount == 2);

    reduct_rvsdg_user_t* predInput = reduct_rvsdg_node_get_input(node, 0);
    assert(predInput != NULL && predInput->edge != NULL);
    reduct_rvsdg_origin_t* predOrigin = predInput->edge->origin;

    if (target == REDUCT_REGISTER_INVALID)
    {
        target = reduct_emitter_reg_alloc(emitter);
    }

    uint32_t skipIdx = 0;
    uint32_t jmpfIdx = 0;
    bool fusedSkip = false;
    bool skipInverted = false;

    reduct_rvsdg_node_t* predNode = (predOrigin->ownerKind == REDUCT_RVSDG_OWNER_NODE) ? predOrigin->node : NULL;

    if (predNode != NULL && predNode->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE &&
        REDUCT_OPCODE_IS_COMPARE(predNode->opcode) && predOrigin->useCount == 1)
    {
        reduct_rvsdg_origin_t* inputB = predNode->firstInput->edge->origin;
        reduct_rvsdg_origin_t* inputC = predNode->firstInput->next->edge->origin;

        reduct_emitter_expr_t exprB = reduct_emit_origin(emitter, inputB, REDUCT_REGISTER_INVALID);
        reduct_emitter_expr_t exprC = reduct_emit_origin(emitter, inputC, REDUCT_REGISTER_INVALID);

        reduct_reg_t regB = reduct_emitter_expr_flush(emitter, &exprB, REDUCT_REGISTER_INVALID);
        reduct_emitter_cache_release(emitter, inputB);
        reduct_emitter_cache_release(emitter, inputC);

        reduct_opcode_t skipOp = REDUCT_OPCODE_TO_SKIP(predNode->opcode);
        skipIdx = emitter->function->instCount;
        reduct_emit_inst_abc(emitter, skipOp, 0, regB, &exprC);

        jmpfIdx = emitter->function->instCount;
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_SAXC(REDUCT_OPCODE_JMP, 0, 0));
        fusedSkip = true;
    }
    else
    {
        reduct_emitter_expr_t predExpr = reduct_emit_origin(emitter, predOrigin, REDUCT_REGISTER_INVALID);
        reduct_reg_t predReg = reduct_emitter_expr_flush(emitter, &predExpr, REDUCT_REGISTER_INVALID);
        reduct_emitter_cache_release(emitter, predOrigin);

        jmpfIdx = emitter->function->instCount;
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_SAXC(REDUCT_OPCODE_JMPF, 0, predReg));
    }

    reduct_rvsdg_region_t* trueRegion = node->firstRegion->next;
    reduct_emitter_expr_t trueExpr = reduct_emit_origin(emitter, trueRegion->result->edge->origin, target);
    reduct_emitter_expr_flush(emitter, &trueExpr, target);

    uint32_t trueInstCount = emitter->function->instCount - (jmpfIdx + 1);
    if (fusedSkip && trueInstCount == 1 &&
        REDUCT_OPCODE_IS_TERMINATOR(REDUCT_INST_GET_OP(emitter->function->insts[jmpfIdx + 1])))
    {
        reduct_inst_t* skipInst = &emitter->function->insts[skipIdx];
        reduct_opcode_t invertedOp = REDUCT_OPCODE_INVERT_SKIP(REDUCT_INST_GET_OP(*skipInst));

        *skipInst = REDUCT_INST_SET_OP(*skipInst, invertedOp);

        emitter->function->insts[jmpfIdx] = emitter->function->insts[jmpfIdx + 1];
        emitter->function->instCount--;

        skipInverted = true;
    }

    bool trueIsTerminated = false;
    if (emitter->function->instCount > 0)
    {
        reduct_inst_t lastInst = emitter->function->insts[emitter->function->instCount - 1];
        if (REDUCT_OPCODE_IS_TERMINATOR(REDUCT_INST_GET_OP(lastInst)))
        {
            trueIsTerminated = true;
        }
    }

    uint32_t jmpIdx = 0;
    if (!trueIsTerminated)
    {
        jmpIdx = emitter->function->instCount;
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_SAXC(REDUCT_OPCODE_JMP, 0, 0));
    }

    if (!skipInverted)
    {
        reduct_emitter_patch_jmp_here(emitter, jmpfIdx);
    }

    reduct_rvsdg_region_t* falseRegion = node->firstRegion;
    reduct_emitter_expr_t falseExpr = reduct_emit_origin(emitter, falseRegion->result->edge->origin, target);
    reduct_emitter_expr_flush(emitter, &falseExpr, target);

    if (!trueIsTerminated)
    {
        reduct_emitter_patch_jmp_here(emitter, jmpIdx);
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        return REDUCT_EMITTER_EXPR_NONE;
    }

    return REDUCT_EMITTER_EXPR_REG(target);
}

static reduct_emitter_expr_t reduct_emit_node(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node, reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(node != NULL);

    reduct_emitter_expr_t result;
    switch (node->type)
    {
    case REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE:
    {
        result = reduct_emit_simple_opcode(emitter, node, target);
    }
    break;
    case REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST:
    {
        result = reduct_emit_simple_constant(emitter, node);
    }
    break;
    case REDUCT_RVSDG_NODE_TYPE_LAMBDA:
    {
        result = reduct_emit_lambda(emitter, node);
    }
    break;
    case REDUCT_RVSDG_NODE_TYPE_PHI:
    {
        result = reduct_emit_phi(emitter, node);
    }
    break;
    case REDUCT_RVSDG_NODE_TYPE_GAMMA:
    {
        result = reduct_emit_gamma(emitter, node, target);
    }
    break;
    default:
        REDUCT_ERROR_THROW(emitter->reduct, "unsupported node type %d", node->type);
    }
    return result;
}

static reduct_emitter_expr_t reduct_emit_region_phi(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_rvsdg_region_t* region, reduct_reg_t target)
{
    reduct_rvsdg_node_t* phiNode = region->parent;
    uint32_t recurSlots = (uint32_t)region->argumentCount - (uint32_t)phiNode->inputCount;

    if (origin->index < recurSlots)
    {
        reduct_rvsdg_node_t* lambdaNode = region->firstNode;
        while (lambdaNode != NULL && lambdaNode->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
        {
            lambdaNode = lambdaNode->next;
        }

        if (lambdaNode != NULL && lambdaNode->output != NULL)
        {
            reduct_emitter_expr_t lambdaExpr = reduct_emitter_cache_get(emitter, lambdaNode->output);
            if (lambdaExpr.type == REDUCT_EMITTER_EXPR_TYPE_REG)
            {
                return REDUCT_EMITTER_EXPR_REG((reduct_reg_t)(lambdaExpr.reg + origin->index));
            }
        }
    }
    else
    {
        uint16_t inputIdx = (uint16_t)(origin->index - recurSlots);
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(phiNode, inputIdx);
        if (input != NULL && input->edge != NULL)
        {
            return reduct_emit_origin(emitter, input->edge->origin, target);
        }
    }

    return REDUCT_EMITTER_EXPR_NONE;
}

static reduct_emitter_expr_t reduct_emit_region_gamma(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_rvsdg_region_t* region, reduct_reg_t target)
{
    reduct_rvsdg_node_t* gammaNode = region->parent;
    const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(gammaNode->type);
    uint16_t inputIdx = (uint16_t)(info->dataInputOffset + origin->index);

    reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(gammaNode, inputIdx);
    if (input != NULL && input->edge != NULL)
    {
        return reduct_emit_origin(emitter, input->edge->origin, target);
    }

    return REDUCT_EMITTER_EXPR_NONE;
}

static reduct_emitter_expr_t reduct_emit_region(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_reg_t target)
{
    reduct_rvsdg_region_t* region = origin->region;
    reduct_rvsdg_node_t* parent = region ? region->parent : NULL;

    if (parent != NULL)
    {
        switch (parent->type)
        {
        case REDUCT_RVSDG_NODE_TYPE_PHI:
        {
            reduct_emitter_expr_t expr = reduct_emit_region_phi(emitter, origin, region, target);
            if (expr.type != REDUCT_EMITTER_EXPR_TYPE_NONE)
            {
                return expr;
            }
            break;
        }
        case REDUCT_RVSDG_NODE_TYPE_GAMMA:
        {
            reduct_emitter_expr_t expr = reduct_emit_region_gamma(emitter, origin, region, target);
            if (expr.type != REDUCT_EMITTER_EXPR_TYPE_NONE)
            {
                return expr;
            }
            break;
        }
        default:
            break;
        }
    }

    return REDUCT_EMITTER_EXPR_CONST((reduct_const_t)(origin->index - emitter->function->arity));
}

static reduct_emitter_expr_t reduct_emit_origin(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(origin != NULL);

    reduct_item_t* previousItem = emitter->lastItem;
    reduct_item_t* item = REDUCT_CONTAINER_OF(origin, reduct_item_t, rvsdgOrigin);
    if (item->moduleId != REDUCT_MODULE_ID_NONE)
    {
        emitter->lastItem = item;
    }

    reduct_emitter_expr_t cached = reduct_emitter_cache_get(emitter, origin);
    if (cached.type != REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        return cached;
    }

    reduct_emitter_expr_t result;
    if (origin->ownerKind == REDUCT_RVSDG_OWNER_REGION)
    {
        result = reduct_emit_region(emitter, origin, target);
    }
    else if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE)
    {
        result = reduct_emit_node(emitter, origin->node, target);
    }
    else
    {
        result = REDUCT_EMITTER_EXPR_NONE;
    }

    if (result.type == REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        emitter->lastItem = previousItem;
        return result;
    }

    if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE)
    {
        result.origin = origin;
        reduct_emitter_cache_put(emitter, origin, result);
    }

    emitter->lastItem = previousItem;
    return result;
}

static reduct_function_t* reduct_emit_function(reduct_t* reduct, reduct_rvsdg_node_t* lambda)
{
    assert(reduct != NULL);
    assert(lambda != NULL);
    assert(lambda->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA);
    assert(lambda->firstRegion != NULL);

    reduct_function_t* function = reduct_function_new(reduct);
    reduct_item_t* functionItem = REDUCT_CONTAINER_OF(function, reduct_item_t, function);
    reduct_item_t* lambdaItem = REDUCT_CONTAINER_OF(lambda, reduct_item_t, rvsdgNode);
    functionItem->moduleId = lambdaItem->moduleId;
    functionItem->modulePos = lambdaItem->modulePos;
    if (lambda->flags & REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC)
    {
        function->flags |= REDUCT_FUNCTION_FLAG_VARIADIC;
    }

    reduct_emitter_t emitter = {0};
    reduct_emitter_init(&emitter, reduct, function, lambda);

    reduct_rvsdg_region_t* body = lambda->firstRegion;
    function->arity = (uint32_t)body->argumentCount - (uint32_t)lambda->inputCount;
    reduct_function_set_capture_count(reduct, function, lambda->inputCount);
    emitter.topReg = (uint16_t)function->arity;

    for (uint32_t i = 0; i < function->arity; i++)
    {
        reduct_rvsdg_origin_t* arg = reduct_rvsdg_region_get_argument(body, (uint16_t)i);
        if (arg->useCount > 0)
        {
            reduct_emitter_cache_put(&emitter, arg, REDUCT_EMITTER_EXPR_REG((reduct_reg_t)i));
        }
        else
        {
            reduct_emitter_reg_free(&emitter, (reduct_reg_t)i);
        }
    }

    reduct_emitter_expr_t expr = reduct_emit_origin(&emitter, body->result->edge->origin, REDUCT_REGISTER_RETURN);
    reduct_emitter_expr_flush(&emitter, &expr, REDUCT_REGISTER_RETURN);

    reduct_emitter_deinit(&emitter);
    return function;
}

REDUCT_API reduct_handle_t reduct_emit(reduct_t* reduct, reduct_handle_t graph)
{
    if (!REDUCT_HANDLE_IS_RVSDG_NODE(graph))
    {
        REDUCT_ERROR_THROW(reduct, "expected RVSDG node, got %s", REDUCT_HANDLE_GET_TYPE_STRING(graph));
    }

    reduct_rvsdg_node_t* root = REDUCT_HANDLE_TO_RVSDG_NODE(graph);

    if (root->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
    {
        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(root->type);
        REDUCT_ERROR_THROW(reduct, "expected RVSDG lambda node as root, got %s", info->name);
    }

    return REDUCT_HANDLE_FROM_FUNCTION(reduct_emit_function(reduct, root));
}
