#include "reduct/function.h"
#include "reduct/handle.h"
#include "reduct/opcode.h"
#include "reduct/rvsdg.h"
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/emit.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/intrinsic.h>
#include <reduct/item.h>
#include <reduct/list.h>

#include <stdlib.h>

#define REDUCT_REGISTER_RETURN ((reduct_reg_t)0xFFFE)

static inline void reduct_emit_inst(reduct_emitter_t* emitter, reduct_inst_t inst);
static inline void reduct_emit_inst_abc(reduct_emitter_t* emitter, reduct_opcode_t opcode, int32_t a, int32_t b,
    reduct_emitter_expr_t* c);
static inline reduct_emitter_expr_t reduct_emit_simple_opcode(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target);
static inline reduct_emitter_expr_t reduct_emit_simple_constant(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node);
static inline reduct_emitter_expr_t reduct_emit_lambda(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node);
static inline reduct_emitter_expr_t reduct_emit_node(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target);
static inline reduct_emitter_expr_t reduct_emit_gamma(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target);
static inline reduct_emitter_expr_t reduct_emit_phi(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node);
static inline reduct_emitter_expr_t reduct_emit_origin(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_reg_t target);
static inline void reduct_emitter_expr_flush(reduct_emitter_t* emitter, reduct_emitter_expr_t* expr,
    reduct_reg_t target);

static inline reduct_function_t* reduct_emit_parse_lambda(reduct_t* reduct, reduct_rvsdg_node_t* node);

static inline void reduct_emitter_init(reduct_emitter_t* emitter, reduct_t* reduct, reduct_function_t* function, reduct_rvsdg_node_t* node)
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
    emitter->lastItem = REDUCT_CONTAINER_OF(node, reduct_item_t, rvsdgNode);
}

static inline void reduct_emitter_deinit(reduct_emitter_t* emitter)
{
    assert(emitter != NULL);

    if (emitter->cache != NULL)
    {
        free(emitter->cache);
    }
}

static inline reduct_reg_t reduct_emitter_alloc_register(reduct_emitter_t* emitter)
{
    assert(emitter != NULL);

    size_t index = reduct_bitmap_find_first_clear(emitter->registers, REDUCT_REGISTER_MAX);
    if (index == REDUCT_BITMAP_INDEX_NONE)
    {
        REDUCT_ERROR_THROW(emitter->reduct, "out of registers");
    }
    REDUCT_BITMAP_SET(emitter->registers, index);
    if (index >= emitter->function->registerCount)
    {
        emitter->function->registerCount = (uint16_t)(index + 1);
    }
    return index;
}

static inline reduct_reg_t reduct_emitter_alloc_register_range(reduct_emitter_t* emitter, size_t count)
{
    assert(emitter != NULL);

    if (count == 0)
    {
        return -1;
    }

    size_t consecutive = 0;
    size_t startIndex = 0;

    for (size_t i = 0; i < REDUCT_REGISTER_MAX; i++)
    {
        if (!REDUCT_BITMAP_TEST(emitter->registers, i))
        {
            if (consecutive == 0)
            {
                startIndex = i;
            }
            if (++consecutive == count)
            {
                reduct_bitmap_set_range(emitter->registers, startIndex, count);
                uint16_t required = (uint16_t)(startIndex + count);
                if (required > emitter->function->registerCount)
                {
                    emitter->function->registerCount = required;
                }
                return startIndex;
            }
        }
        else
        {
            consecutive = 0;
        }
    }

    REDUCT_ERROR_THROW(emitter->reduct, "out of registers");
}

static inline void reduct_emitter_set_register(reduct_emitter_t* emitter, reduct_reg_t reg)
{
    assert(emitter != NULL);
    assert(reg < REDUCT_REGISTER_MAX);

    REDUCT_BITMAP_SET(emitter->registers, reg);
}

static inline void reduct_emitter_free_register(reduct_emitter_t* emitter, reduct_reg_t reg)
{
    assert(emitter != NULL);
    assert(reg < REDUCT_REGISTER_MAX);

    REDUCT_BITMAP_CLEAR(emitter->registers, reg);
}

static inline reduct_emitter_expr_t reduct_emitter_cache_get(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin)
{
    if (emitter->cache == NULL || origin == NULL)
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

static inline void reduct_emitter_cache_put(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_emitter_expr_t expr)
{
    assert(origin != NULL);

    if (emitter->cacheCount >= emitter->cacheCapacity)
    {
        size_t newCapacity = emitter->cacheCapacity == 0 ? 16 : emitter->cacheCapacity * 2;
        reduct_emitter_cache_entry_t* newCache =
            realloc(emitter->cache, sizeof(reduct_emitter_cache_entry_t) * newCapacity);
        if (newCache == NULL)
        {
            REDUCT_ERROR_THROW(emitter->reduct, "out of memory for emitter cache");
        }
        emitter->cache = newCache;
        emitter->cacheCapacity = newCapacity;
    }
    emitter->cache[emitter->cacheCount].origin = origin;
    emitter->cache[emitter->cacheCount].expr = expr;
    emitter->cache[emitter->cacheCount].remainingUses = origin->useCount;
    emitter->cacheCount++;
}

static inline void reduct_emitter_cache_update(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_emitter_expr_t expr)
{
    if (emitter->cache == NULL || origin == NULL)
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

static inline void reduct_emitter_release_origin(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin)
{
    if (origin == NULL || emitter->cache == NULL)
    {
        return;
    }

    for (size_t i = 0; i < emitter->cacheCount; i++)
    {
        if (emitter->cache[i].origin != origin)
        {
            continue;
        }

        if (emitter->cache[i].remainingUses == 0)
        {
            return;
        }

        emitter->cache[i].remainingUses--;
        if (emitter->cache[i].remainingUses == 0 && emitter->cache[i].expr.type == REDUCT_EMITTER_EXPR_TYPE_REG)
        {
            reduct_emitter_free_register(emitter, emitter->cache[i].expr.reg);
        }
    }
}

static inline void reduct_emitter_expr_flush(reduct_emitter_t* emitter, reduct_emitter_expr_t* expr,
    reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(expr != NULL);

    if (expr->type == REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        return;
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        if (expr->type == REDUCT_EMITTER_EXPR_TYPE_REG)
        {
            reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RET, 0, 0, expr->reg));
        }
        else if (expr->type == REDUCT_EMITTER_EXPR_TYPE_CONST)
        {
            reduct_emit_inst(emitter,
                REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RET | REDUCT_OPCODE_MODE_CONST, 0, 0, expr->constant));
        }

        expr->type = REDUCT_EMITTER_EXPR_TYPE_NONE;
        return;
    }

    if (target == REDUCT_REGISTER_INVALID)
    {
        if (expr->type == REDUCT_EMITTER_EXPR_TYPE_REG)
        {
            return;
        }
        target = reduct_emitter_alloc_register(emitter);

        if (expr->origin != NULL)
        {
            reduct_emitter_cache_update(emitter, expr->origin, REDUCT_EMITTER_EXPR_REG(target));
        }
    }

    switch (expr->type)
    {
    case REDUCT_EMITTER_EXPR_TYPE_REG:
    {
        if (expr->reg != target)
        {
            reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV, target, 0, expr->reg));
        }
    }
    break;
    case REDUCT_EMITTER_EXPR_TYPE_CONST:
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_MOV_CONST, target, 0, expr->constant));
    }
    break;
    default:
        REDUCT_ERROR_THROW(emitter->reduct, "invalid expression type");
    }

    expr->type = REDUCT_EMITTER_EXPR_TYPE_REG;
    expr->reg = target;
}

static inline reduct_inst_t reduct_emitter_make_inst(reduct_emitter_t* emitter, reduct_opcode_t opcode, int32_t a, int32_t b,
    reduct_emitter_expr_t* c)
{
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
    return inst;
}

static inline void reduct_emit_inst(reduct_emitter_t* emitter, reduct_inst_t inst)
{
    assert(emitter != NULL);
    uint32_t pos = emitter->lastItem != NULL ? emitter->lastItem->position : 0;
    reduct_function_emit(emitter->reduct, emitter->function, inst, pos);
}

static inline void reduct_emit_inst_abc(reduct_emitter_t* emitter, reduct_opcode_t opcode, int32_t a, int32_t b,
    reduct_emitter_expr_t* c)
{
    assert(emitter != NULL);
    assert(c != NULL);

    reduct_inst_t inst = reduct_emitter_make_inst(emitter, opcode, a, b, c);
    reduct_emit_inst(emitter, inst);
}

static inline reduct_emitter_expr_t reduct_emit_origin(reduct_emitter_t* emitter, reduct_rvsdg_origin_t* origin,
    reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(origin != NULL);

    if (origin->ownerKind == REDUCT_RVSDG_OWNER_REGION)
    {
        reduct_rvsdg_region_t* reg = origin->region;
        if (reg != NULL && reg->parent != NULL && reg->parent->type == REDUCT_RVSDG_NODE_TYPE_PHI)
        {
            reduct_rvsdg_node_t* phiNode = reg->parent;
            uint32_t recurSlots = (uint32_t)reg->argumentCount - (uint32_t)phiNode->inputCount;
            if (origin->index < recurSlots)
            {
                reduct_rvsdg_node_t* lambdaNode = reg->firstNode;
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
                if (input != NULL && input->use != NULL)
                {
                    return reduct_emit_origin(emitter, input->use->origin, target);
                }
            }
        }

        if (reg != NULL && reg->parent != NULL && reg->parent->type == REDUCT_RVSDG_NODE_TYPE_GAMMA)
        {
            const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(reg->parent->type);
            uint16_t dataIdx = (uint16_t)(info->dataInputOffset + origin->index);
            reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(reg->parent, dataIdx);
            if (input != NULL && input->use != NULL)
            {
                return reduct_emit_origin(emitter, input->use->origin, target);
            }
        }

        if (origin->index < emitter->function->arity)
        {
            return REDUCT_EMITTER_EXPR_REG(origin->index);
        }
        return REDUCT_EMITTER_EXPR_CONST((reduct_const_t)(origin->index - emitter->function->arity));
    }

    if (origin->ownerKind == REDUCT_RVSDG_OWNER_NODE)
    {
        reduct_emitter_expr_t cached = reduct_emitter_cache_get(emitter, origin);
        if (cached.type != REDUCT_EMITTER_EXPR_TYPE_NONE)
        {
            return cached;
        }

        return reduct_emit_node(emitter, origin->node, target);
    }

    return REDUCT_EMITTER_EXPR_NONE;
}

static inline bool reduct_emitter_origin_is_phi_recur(reduct_emitter_t* emitter, const reduct_rvsdg_origin_t* origin)
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

            uint16_t inputIdx;
            if (parent != NULL && reduct_rvsdg_node_map_argument_to_input(parent, region, origin->index, &inputIdx))
            {
                reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(parent, inputIdx);
                if (input == NULL || input->use == NULL)
                {
                    return false;
                }
                origin = input->use->origin;
                continue;
            }
        }

        break;
    }

    return false;
}

static inline reduct_emitter_expr_t reduct_emitter_emit_range(reduct_emitter_t* emitter,
    reduct_rvsdg_node_t* node, reduct_opcode_t op, reduct_reg_t target)
{
    uint32_t arity;
    uint32_t inputIdx = 0;
    reduct_emitter_expr_t cExpr = REDUCT_EMITTER_EXPR_REG(REDUCT_REGISTER_INVALID);
    bool isRecur = false;

    if (op == REDUCT_OPCODE_CALL)
    {
        reduct_rvsdg_user_t* callableInput = reduct_rvsdg_node_get_input(node, 0);
        if (callableInput != NULL && callableInput->use != NULL &&
            reduct_emitter_origin_is_phi_recur(emitter, callableInput->use->origin))
        {
            isRecur = true;
            op = REDUCT_OPCODE_RECUR;
        }
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        op = REDUCT_OPCODE_TO_TAIL(op);
    }

    if (!isRecur && REDUCT_OPCODE_READS_C(op))
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, 0);
        if (input == NULL || input->use == NULL)
        {
            REDUCT_ERROR_THROW(emitter->reduct, "call opcode expects callable at input 0");
        }
        cExpr = reduct_emit_origin(emitter, input->use->origin, REDUCT_REGISTER_INVALID);
        arity = node->inputCount - 1;
        inputIdx = 1;
    }
    else if (isRecur)
    {
        arity = node->inputCount > 0 ? (uint32_t)node->inputCount - 1 : 0;
        inputIdx = 1;
    }
    else
    {
        arity = node->inputCount;
    }

    size_t allocCount = arity > 0 ? arity : 1;
    reduct_reg_t a = reduct_emitter_alloc_register_range(emitter, allocCount);

    for (uint32_t i = 0; i < arity; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, (uint16_t)(inputIdx + i));
        if (input != NULL && input->use != NULL)
        {
            reduct_emitter_expr_t argExpr =
                reduct_emit_origin(emitter, input->use->origin, (reduct_reg_t)(a + i));
            reduct_emitter_expr_flush(emitter, &argExpr, (reduct_reg_t)(a + i));
        }
    }

    if (!isRecur && REDUCT_OPCODE_READS_C(op))
    {
        reduct_emit_inst_abc(emitter, op, (int32_t)a, (int32_t)arity, &cExpr);
    }
    else
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(op, a, arity, 0));
    }

    for (uint32_t i = 0; i < arity; i++)
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, (uint16_t)(inputIdx + i));
        if (input != NULL && input->use != NULL)
        {
            reduct_emitter_cache_update(emitter, input->use->origin, REDUCT_EMITTER_EXPR_NONE);
            reduct_emitter_release_origin(emitter, input->use->origin);
        }
    }

    if (!isRecur && REDUCT_OPCODE_READS_C(op))
    {
        reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, 0);
        if (input != NULL && input->use != NULL)
        {
            reduct_emitter_cache_update(emitter, input->use->origin, REDUCT_EMITTER_EXPR_NONE);
            reduct_emitter_release_origin(emitter, input->use->origin);
        }
    }

    for (uint32_t i = 1; i < allocCount; i++)
    {
        reduct_emitter_free_register(emitter, (reduct_reg_t)(a + i));
    }

    if (target == REDUCT_REGISTER_RETURN)
    {
        reduct_emitter_free_register(emitter, a);
        return REDUCT_EMITTER_EXPR_NONE;
    }

    return REDUCT_EMITTER_EXPR_REG(a);
}

static inline reduct_emitter_expr_t reduct_emitter_emit_binary(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_opcode_t op, reduct_reg_t target)
{
    reduct_rvsdg_user_t* inB = reduct_rvsdg_node_get_input(node, 0);
    reduct_rvsdg_user_t* inC = reduct_rvsdg_node_get_input(node, 1);

    reduct_rvsdg_origin_t* origB = inB->use->origin;
    reduct_rvsdg_origin_t* origC = inC->use->origin;

    reduct_emitter_expr_t bExpr = reduct_emit_origin(emitter, origB, REDUCT_REGISTER_INVALID);
    reduct_emitter_expr_t cExpr = reduct_emit_origin(emitter, origC, REDUCT_REGISTER_INVALID);

    if (REDUCT_OPCODE_IS_COMMUTATIVE(op) && bExpr.type == REDUCT_EMITTER_EXPR_TYPE_CONST &&
        cExpr.type != REDUCT_EMITTER_EXPR_TYPE_CONST)
    {
        reduct_emitter_expr_t temp = bExpr;
        bExpr = cExpr;
        cExpr = temp;
    }

    reduct_emitter_expr_flush(emitter, &bExpr, REDUCT_REGISTER_INVALID);
    reduct_reg_t bReg = bExpr.reg;

    reduct_emitter_release_origin(emitter, origB);
    reduct_emitter_release_origin(emitter, origC);

    reduct_reg_t outReg = target;
    if ((outReg == REDUCT_REGISTER_INVALID || outReg == REDUCT_REGISTER_RETURN) && REDUCT_OPCODE_HAS_TARGET(op))
    {
        outReg = reduct_emitter_alloc_register(emitter);
    }
    reduct_emit_inst_abc(emitter, op, outReg, bReg, &cExpr);

    return REDUCT_EMITTER_EXPR_REG(outReg);
}

static inline reduct_emitter_expr_t reduct_emitter_emit_unary(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_opcode_t op, reduct_reg_t target)
{
    reduct_rvsdg_user_t* input = reduct_rvsdg_node_get_input(node, 0);
    reduct_rvsdg_origin_t* origin = input->use->origin;
    reduct_emitter_expr_t cExpr = reduct_emit_origin(emitter, origin, REDUCT_REGISTER_INVALID);

    reduct_emitter_release_origin(emitter, origin);

    reduct_reg_t outReg = target;
    if ((outReg == REDUCT_REGISTER_INVALID || outReg == REDUCT_REGISTER_RETURN) && REDUCT_OPCODE_HAS_TARGET(op))
    {
        outReg = reduct_emitter_alloc_register(emitter);
    }
    reduct_emit_inst_abc(emitter, op, outReg, 0, &cExpr);

    return REDUCT_EMITTER_EXPR_REG(outReg);
}

static inline reduct_emitter_expr_t reduct_emit_simple_opcode(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    reduct_opcode_t op = node->opcode;

    if (REDUCT_OPCODE_READS_RANGE(op))
    {
        return reduct_emitter_emit_range(emitter, node, op, target);
    }
    if (REDUCT_OPCODE_IS_BINARY(op))
    {
        return reduct_emitter_emit_binary(emitter, node, op, target);
    }
    if (REDUCT_OPCODE_IS_UNARY(op))
    {
        return reduct_emitter_emit_unary(emitter, node, op, target);
    }

    if (op == REDUCT_OPCODE_NOP)
    {
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_NOP, 0, 0, 0));
        return REDUCT_EMITTER_EXPR_REG(REDUCT_REGISTER_INVALID);
    }

    REDUCT_ERROR_THROW(emitter->reduct, "unsupported opcode %d", op);
}

static inline reduct_emitter_expr_t reduct_emit_simple_constant(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    reduct_const_t constant = reduct_function_add_constant(emitter->reduct, emitter->function, node->constant);
    return REDUCT_EMITTER_EXPR_CONST(constant);
}

static inline reduct_emitter_expr_t reduct_emit_lambda(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    reduct_reg_t target = reduct_emitter_alloc_register(emitter);
    reduct_emitter_expr_t result = REDUCT_EMITTER_EXPR_REG(target);
    reduct_emitter_cache_put(emitter, node->output, result);

    reduct_function_t* function = reduct_emit_parse_lambda(emitter->reduct, node);
    reduct_const_t constant =
        reduct_function_add_constant(emitter->reduct, emitter->function, REDUCT_HANDLE_FROM_FUNCTION(function));

    reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_CLOSURE, target, 0, constant));

    reduct_rvsdg_user_t* input = node->firstInput;
    while (input != NULL)
    {
        if (input->use != NULL)
        {
            reduct_rvsdg_origin_t* origin = input->use->origin;
            reduct_emitter_expr_t valExpr = reduct_emit_origin(emitter, origin, REDUCT_REGISTER_INVALID);
            reduct_emit_inst_abc(emitter, REDUCT_OPCODE_CAPTURE, target, input->index, &valExpr);
            reduct_emitter_release_origin(emitter, origin);
        }
        input = input->next;
    }

    return result;
}

static inline uint32_t reduct_emit_skip_condition(reduct_emitter_t* emitter,
                                                   reduct_rvsdg_node_t* cmpNode)
{
    assert(emitter != NULL && cmpNode != NULL);
    assert(REDUCT_OPCODE_IS_COMPARE(cmpNode->opcode));

    reduct_opcode_t skipOp = REDUCT_OPCODE_TO_SKIP(cmpNode->opcode);
    assert(skipOp != REDUCT_OPCODE_NOP);

    reduct_rvsdg_user_t* inB = reduct_rvsdg_node_get_input(cmpNode, 0);
    reduct_rvsdg_user_t* inC = reduct_rvsdg_node_get_input(cmpNode, 1);
    REDUCT_ERROR_ASSERT(emitter->reduct,
        inB != NULL && inB->use != NULL, "comparison node missing operand B");
    REDUCT_ERROR_ASSERT(emitter->reduct,
        inC != NULL && inC->use != NULL, "comparison node missing operand C");

    reduct_rvsdg_origin_t* origB = inB->use->origin;
    reduct_rvsdg_origin_t* origC = inC->use->origin;

    reduct_emitter_expr_t bExpr = reduct_emit_origin(emitter, origB, REDUCT_REGISTER_INVALID);
    reduct_emitter_expr_t cExpr = reduct_emit_origin(emitter, origC, REDUCT_REGISTER_INVALID);

    if (REDUCT_OPCODE_IS_COMMUTATIVE(cmpNode->opcode) &&
        bExpr.type == REDUCT_EMITTER_EXPR_TYPE_CONST &&
        cExpr.type != REDUCT_EMITTER_EXPR_TYPE_CONST)
    {
        reduct_emitter_expr_t tmp = bExpr;
        bExpr = cExpr;
        cExpr = tmp;
    }

    reduct_emitter_expr_flush(emitter, &bExpr, REDUCT_REGISTER_INVALID);

    reduct_emitter_release_origin(emitter, origB);
    reduct_emitter_release_origin(emitter, origC);

    reduct_emit_inst_abc(emitter, skipOp, 0, (int32_t)bExpr.reg, &cExpr);

    uint32_t jmpIdx = emitter->function->instCount;
    reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_JMP, 0, 0, 0));

    return jmpIdx;
}

static inline bool reduct_emitter_can_use_skip(const reduct_rvsdg_origin_t* origin)
{
    if (origin == NULL || origin->ownerKind != REDUCT_RVSDG_OWNER_NODE)
    {
        return false;
    }

    const reduct_rvsdg_node_t* node = origin->node;
    if (node->type != REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
    {
        return false;
    }
    if (!REDUCT_OPCODE_IS_COMPARE(node->opcode))
    {
        return false;
    }

    return origin->useCount == 1;
}

static inline reduct_emitter_expr_t reduct_emit_gamma(reduct_emitter_t* emitter,
    reduct_rvsdg_node_t* node, reduct_reg_t target)
{
    assert(emitter != NULL && node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_GAMMA);
    REDUCT_ERROR_ASSERT(emitter->reduct, node->regionCount == 2,
        "gamma node must have exactly 2 regions for now");

    reduct_rvsdg_user_t* condInput = reduct_rvsdg_node_get_input(node, 0);
    reduct_rvsdg_origin_t* condOrigin = condInput->use->origin;

    reduct_reg_t targetReg = target;
    if (targetReg == REDUCT_REGISTER_INVALID)
    {
        targetReg = reduct_emitter_alloc_register(emitter);
    }

    uint32_t jmpElseIdx;
    if (reduct_emitter_can_use_skip(condOrigin))
    {
        jmpElseIdx = reduct_emit_skip_condition(emitter, condOrigin->node);
    }
    else
    {
        reduct_emitter_expr_t condExpr =
            reduct_emit_origin(emitter, condOrigin, REDUCT_REGISTER_INVALID);
        reduct_emitter_expr_flush(emitter, &condExpr, REDUCT_REGISTER_INVALID);

        jmpElseIdx = emitter->function->instCount;
        reduct_emit_inst_abc(emitter, REDUCT_OPCODE_JMPF, 0, 0, &condExpr);

        reduct_emitter_release_origin(emitter, condOrigin);
    }

    {
        reduct_rvsdg_region_t* thenRegion = node->firstRegion->next;
        reduct_rvsdg_user_t* thenResult = thenRegion->result;
        reduct_emitter_expr_t resExpr =
            reduct_emit_origin(emitter, thenResult->use->origin, targetReg);
        reduct_emitter_expr_flush(emitter, &resExpr, targetReg);
        reduct_emitter_release_origin(emitter, thenResult->use->origin);
    }

    uint32_t jmpEndIdx = 0;
    if (targetReg != REDUCT_REGISTER_RETURN)
    {
        jmpEndIdx = emitter->function->instCount;
        reduct_emit_inst(emitter, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_JMP, 0, 0, 0));
    }

    {
        int32_t sax = (int32_t)(emitter->function->instCount - jmpElseIdx - 1);
        emitter->function->insts[jmpElseIdx] =
            REDUCT_INST_SET_SAX(emitter->function->insts[jmpElseIdx], sax);
    }

    {
        reduct_rvsdg_region_t* elseRegion = node->firstRegion;
        reduct_rvsdg_user_t* elseResult = elseRegion->result;
        reduct_emitter_expr_t resExpr =
            reduct_emit_origin(emitter, elseResult->use->origin, targetReg);
        reduct_emitter_expr_flush(emitter, &resExpr, targetReg);
        reduct_emitter_release_origin(emitter, elseResult->use->origin);
    }

    if (targetReg != REDUCT_REGISTER_RETURN)
    {
        int32_t sax = (int32_t)(emitter->function->instCount - jmpEndIdx - 1);
        emitter->function->insts[jmpEndIdx] =
            REDUCT_INST_SET_SAX(emitter->function->insts[jmpEndIdx], sax);
    }

    if (targetReg == REDUCT_REGISTER_RETURN)
    {
        return REDUCT_EMITTER_EXPR_NONE;
    }
    return REDUCT_EMITTER_EXPR_REG(targetReg);
}

static inline reduct_emitter_expr_t reduct_emit_phi(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node)
{
    assert(emitter != NULL && node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_PHI);
    return reduct_emit_origin(emitter, node->firstRegion->result->use->origin, REDUCT_REGISTER_INVALID);
}

static inline reduct_emitter_expr_t reduct_emit_node(reduct_emitter_t* emitter, reduct_rvsdg_node_t* node,
    reduct_reg_t target)
{
    assert(emitter != NULL);
    assert(node != NULL);

    if (node->output != NULL)
    {
        reduct_emitter_expr_t cached = reduct_emitter_cache_get(emitter, node->output);
        if (cached.type != REDUCT_EMITTER_EXPR_TYPE_NONE)
        {
            return cached;
        }
    }

    reduct_item_t* previousItem = emitter->lastItem;
    reduct_item_t* item = REDUCT_CONTAINER_OF(node, reduct_item_t, rvsdgNode);
    if (item->inputId != REDUCT_INPUT_ID_NONE)
    {
        emitter->lastItem = item;
    }

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
    case REDUCT_RVSDG_NODE_TYPE_GAMMA:
    {
        result = reduct_emit_gamma(emitter, node, target);
    }
    break;
    case REDUCT_RVSDG_NODE_TYPE_PHI:
    {
        result = reduct_emit_phi(emitter, node);
    }
    break;
    default:
        REDUCT_ERROR_THROW(emitter->reduct, "unsupported node type %d", node->type);
    }

    if (result.type == REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        emitter->lastItem = previousItem;
        return result;
    }

    reduct_rvsdg_origin_t* output = node->output;
    result.origin = output;

    if (reduct_emitter_cache_get(emitter, output).type != REDUCT_EMITTER_EXPR_TYPE_NONE)
    {
        emitter->lastItem = previousItem;
        return result;
    }

    reduct_emitter_expr_t entryExpr;
    switch (result.type)
    {
    case REDUCT_EMITTER_EXPR_TYPE_REG:
        entryExpr = REDUCT_EMITTER_EXPR_REG(result.reg);
        break;
    case REDUCT_EMITTER_EXPR_TYPE_CONST:
        entryExpr = REDUCT_EMITTER_EXPR_CONST(result.constant);
        break;
    default:
        entryExpr = REDUCT_EMITTER_EXPR_NONE;
        break;
    }
    entryExpr.origin = output;
    reduct_emitter_cache_put(emitter, output, entryExpr);

    emitter->lastItem = previousItem;
    return result;
}

static inline reduct_function_t* reduct_emit_parse_lambda(reduct_t* reduct, reduct_rvsdg_node_t* node)
{
    assert(reduct != NULL);
    assert(node != NULL);
    assert(node->type == REDUCT_RVSDG_NODE_TYPE_LAMBDA);

    reduct_function_t* function = reduct_function_new(reduct);
    reduct_item_t* functionItem = REDUCT_CONTAINER_OF(function, reduct_item_t, function);
    reduct_item_t* lambdaItem = REDUCT_CONTAINER_OF(node, reduct_item_t, rvsdgNode);
    functionItem->inputId = lambdaItem->inputId;
    functionItem->position = lambdaItem->position;
    if (node->flags & REDUCT_RVSDG_NODE_FLAGS_LAMBDA_VARIADIC)
    {
        function->flags |= REDUCT_FUNCTION_FLAG_VARIADIC;
    }

    reduct_emitter_t emitter = {0};
    reduct_emitter_init(&emitter, reduct, function, node);

    reduct_rvsdg_region_t* body = node->firstRegion;
    function->arity = (uint32_t)body->argumentCount - (uint32_t)node->inputCount;
    for (uint16_t i = 0; i < (uint16_t)node->inputCount; i++)
    {
        reduct_function_add_capture(reduct, function);
    }
    for (uint32_t i = 0; i < function->arity; i++)
    {
        reduct_emitter_set_register(&emitter, i);
    }
    function->registerCount = (uint16_t)function->arity;

    reduct_emitter_expr_t expr = reduct_emit_origin(&emitter, body->result->use->origin, REDUCT_REGISTER_RETURN);
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

    return REDUCT_HANDLE_FROM_FUNCTION(reduct_emit_parse_lambda(reduct, root));
}