#include <reduct/build.h>
#include <reduct/closure.h>
#include <reduct/defs.h>
#include <reduct/emit.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>
#include <reduct/optimize.h>
#include <reduct/parse.h>
#include <reduct/standard.h>
#include <stdarg.h>

REDUCT_API void reduct_eval_local_init(reduct_eval_local_t* local)
{
    assert(local != NULL);
    local->frames = NULL;
    local->frameCount = 0;
    local->frameCapacity = 0;
    local->regs = NULL;
    local->regCount = 0;
    local->regCapacity = 0;
}

REDUCT_API void reduct_eval_local_deinit(reduct_eval_local_t* local)
{
    assert(local != NULL);
    if (local->frames != NULL)
    {
        free(local->frames);
        local->frames = NULL;
    }
    if (local->regs != NULL)
    {
        free(local->regs);
        local->regs = NULL;
    }
    local->frameCount = 0;
    local->frameCapacity = 0;
    local->regCount = 0;
    local->regCapacity = 0;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_ensure_regs(reduct_t* reduct, uint32_t neededRegs)
{
    assert(reduct != NULL);

    if (REDUCT_LIKELY(neededRegs <= reduct->eval.regCapacity))
    {
        return;
    }

    uint32_t oldCapacity = reduct->eval.regCapacity;
    while (neededRegs > reduct->eval.regCapacity)
    {
        reduct->eval.regCapacity *= REDUCT_EVAL_REGS_GROWTH_FACTOR;
    }
    if (REDUCT_UNLIKELY(reduct->eval.regCapacity > REDUCT_EVAL_REGS_MAX))
    {
        REDUCT_ERROR_INTERNAL(reduct, "register overflow within evaluator, most likely caused by excessive recursion");
    }

    reduct_handle_t* newRegs =
        (reduct_handle_t*)realloc(reduct->eval.regs, sizeof(reduct_handle_t) * reduct->eval.regCapacity);
    if (newRegs == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memset(newRegs + oldCapacity, 0, (reduct->eval.regCapacity - oldCapacity) * sizeof(reduct_handle_t));
    reduct->eval.regs = newRegs;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_push_frame(reduct_t* reduct, reduct_closure_t* closure,
    uint32_t target)
{
    assert(reduct != NULL);
    assert(closure != NULL);

    if (REDUCT_UNLIKELY(reduct->eval.frameCount >= reduct->eval.frameCapacity))
    {
        if (REDUCT_UNLIKELY(reduct->eval.frameCapacity * REDUCT_EVAL_FRAMES_GROWTH_FACTOR >= REDUCT_EVAL_FRAMES_MAX))
        {
            REDUCT_ERROR_INTERNAL(reduct, "stack overflow");
        }

        reduct->eval.frameCapacity *= REDUCT_EVAL_FRAMES_GROWTH_FACTOR;
        reduct->eval.frames = (reduct_eval_frame_t*)realloc(reduct->eval.frames,
            sizeof(reduct_eval_frame_t) * reduct->eval.frameCapacity);
        if (reduct->eval.frames == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    uint32_t neededRegs = target + closure->function->registerCount;
    reduct_eval_ensure_regs(reduct, neededRegs);

    reduct_eval_frame_t* frame = &reduct->eval.frames[reduct->eval.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->insts;
    frame->constants = closure->constants;
    frame->base = target;
    frame->prevRegCount = reduct->eval.regCount;

    reduct->eval.regCount = neededRegs;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_pop_frame(reduct_t* reduct)
{
    assert(reduct->eval.frameCount > 0);

    reduct_eval_frame_t* frame = &reduct->eval.frames[--reduct->eval.frameCount];

    reduct->eval.regCount = frame->prevRegCount;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_tail_frame(reduct_t* reduct, reduct_closure_t* closure)
{
    assert(reduct != NULL);
    assert(reduct->eval.frameCount > 0);
    assert(closure != NULL);

    reduct_eval_frame_t* frame = &reduct->eval.frames[reduct->eval.frameCount - 1];

    uint32_t neededRegs = frame->base + closure->function->registerCount;
    reduct_eval_ensure_regs(reduct, neededRegs);
    reduct->eval.regCount = neededRegs;

    frame->closure = closure;
    frame->ip = closure->function->insts;
    frame->constants = closure->constants;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_ensure_ready(reduct_t* reduct)
{
    if (REDUCT_UNLIKELY(reduct->eval.frameCapacity == 0))
    {
        reduct->eval.frameCapacity = REDUCT_EVAL_FRAMES_INITIAL;
        reduct->eval.frames = (reduct_eval_frame_t*)calloc(1, sizeof(reduct_eval_frame_t) * reduct->eval.frameCapacity);
        if (reduct->eval.frames == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    if (REDUCT_UNLIKELY(reduct->eval.regCapacity == 0))
    {
        reduct->eval.regCapacity = REDUCT_EVAL_REGS_INITIAL;
        reduct->eval.regs = (reduct_handle_t*)calloc(1, sizeof(reduct_handle_t) * reduct->eval.regCapacity);
        if (reduct->eval.regs == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
}

static inline REDUCT_ALWAYS_INLINE uint32_t reduct_eval_bundle_args(reduct_t* reduct, reduct_function_t* func,
    uint32_t argc, reduct_handle_t* argv)
{
    uint32_t arity = func->arity;
    if (REDUCT_UNLIKELY(func->flags & REDUCT_FUNCTION_FLAG_VARIADIC))
    {
        REDUCT_ERROR_ASSERT(reduct, argc >= (uint32_t)arity - 1, "expected at least %u arguments, got %u", arity - 1,
            argc);

        uint32_t fixed = arity - 1;
        argv[fixed] = REDUCT_HANDLE_CREATE_HANDLES(reduct, argc - fixed, &argv[fixed]);
        return arity;
    }
    REDUCT_ERROR_ASSERT(reduct, argc == arity, "expected %u arguments, got %u", arity, argc);
    return argc;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((hot))
#endif
static reduct_handle_t reduct_eval_run(reduct_t* reduct, uint32_t initialFrameCount)
{
    assert(reduct != NULL);

    reduct_eval_frame_t* frame = &reduct->eval.frames[reduct->eval.frameCount - 1];
    reduct_inst_t* ip = frame->ip;
    reduct_handle_t* regs = reduct->eval.regs;
    reduct_handle_t* r = regs + frame->base;
    reduct_handle_t* k = frame->constants;
    reduct_inst_t inst;
    reduct_handle_t result = REDUCT_HANDLE_NIL(reduct);
    bool shouldFork = reduct_task_queue_size(&reduct->global->task) <= reduct->global->threadCount * 2;

    // clang-format off
    /// @todo Write some macro magic to turn this into a switch case if not using GCC or Clang.

#define DISPATCH() \
    inst = *ip++; \
    frame->ip = ip; \
    goto* dispatchTable[REDUCT_INST_GET_OP(inst)]

#define UPDATE_STATE() \
    regs = reduct->eval.regs; \
    frame = &reduct->eval.frames[reduct->eval.frameCount - 1]; \
    ip = frame->ip; \
    r = regs + frame->base; \
    k = frame->constants

#define UPDATE_BASE() \
    regs = reduct->eval.regs; \
    r = regs + frame->base

#define DECODE_A() uint32_t a = REDUCT_INST_GET_A(inst)
#define DECODE_B() uint32_t b = REDUCT_INST_GET_B(inst)
#define DECODE_C() uint32_t c = REDUCT_INST_GET_C(inst)
#define DECODE_C_REG() \
    uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = r[c]
#define DECODE_C_CONST() \
    uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = k[c]
#define DECODE_SAX() int32_t sax = REDUCT_INST_GET_SAX(inst)

#define OP_BITWISE(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    REDUCT_HANDLE_BITWISE_FAST(reduct, &r[a], r[b], valC, _op); \
    DISPATCH(); \
})

#define OP_COMPARE(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    r[a] = REDUCT_HANDLE_FROM_BOOL(reduct, REDUCT_HANDLE_COMPARE_FAST(reduct, r[b], valC, _op)); \
    DISPATCH(); \
})

#define OP_ARITH(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &r[a], r[b], valC, _op); \
    DISPATCH(); \
})

#define OP_SHIFT(_label, _op, _name) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    int64_t amount = reduct_handle_as_int(reduct, valC); \
    REDUCT_ERROR_ASSERT(reduct, amount >= 0 && amount < REDUCT_HANDLE_NUMBER_WIDTH - 1, _name " shift amount must be 0-%ld, got %ld", (long)(REDUCT_HANDLE_NUMBER_WIDTH - 1), (long)amount); \
    r[a] = REDUCT_HANDLE_FROM_NUMBER((double)(reduct_handle_as_int(reduct, r[b]) _op amount)); \
    DISPATCH(); \
})

#define OP_SKIP_CMP(_label, _op) \
    _label: \
    { \
        DECODE_B(); \
        DECODE_C(); \
        if (REDUCT_HANDLE_COMPARE_FAST(reduct, r[b], r[c], _op)) \
        { \
            ip++; \
        } \
        DISPATCH(); \
    } \
    _label##_k: \
    { \
        DECODE_B(); \
        DECODE_C_CONST(); \
        if (REDUCT_HANDLE_COMPARE_FAST(reduct, r[b], valC, _op)) \
        { \
            ip++; \
        } \
        DISPATCH(); \
    }

    const void* dispatchTable[256] = {
        [0 ... 255] = &&label_invalid,
        [REDUCT_OPCODE_NOP] = &&label_nop,
        [REDUCT_OPCODE_MOV] = &&label_mov, [REDUCT_OPCODE_MOV_CONST] = &&label_mov_k,
        [REDUCT_OPCODE_LIST] = &&label_list,
        [REDUCT_OPCODE_CLOSURE] = &&label_closure,
        [REDUCT_OPCODE_CAPTURE] = &&label_capture, [REDUCT_OPCODE_CAPTURE_CONST] = &&label_capture_k,
        [REDUCT_OPCODE_JMP] = &&label_jmp,
        [REDUCT_OPCODE_JMPF] = &&label_jmpf,
        [REDUCT_OPCODE_JMPT] = &&label_jmpt,
        [REDUCT_OPCODE_CALL] = &&label_call, [REDUCT_OPCODE_CALL_CONST] = &&label_call_k,
        [REDUCT_OPCODE_RET] = &&label_ret, [REDUCT_OPCODE_RET_CONST] = &&label_ret_k,
        [REDUCT_OPCODE_TAILCALL] = &&label_tailcall, [REDUCT_OPCODE_TAILCALL_CONST] = &&label_tailcall_k,
        [REDUCT_OPCODE_RECUR] = &&label_recur,
        [REDUCT_OPCODE_TAILRECUR] = &&label_tailrecur,
        [REDUCT_OPCODE_EQ] = &&label_eq, [REDUCT_OPCODE_EQ_CONST] = &&label_eq_k,
        [REDUCT_OPCODE_NEQ] = &&label_neq, [REDUCT_OPCODE_NEQ_CONST] = &&label_neq_k,
        [REDUCT_OPCODE_LT] = &&label_lt, [REDUCT_OPCODE_LT_CONST] = &&label_lt_k,
        [REDUCT_OPCODE_LE] = &&label_le, [REDUCT_OPCODE_LE_CONST] = &&label_le_k,
        [REDUCT_OPCODE_GT] = &&label_gt, [REDUCT_OPCODE_GT_CONST] = &&label_gt_k,
        [REDUCT_OPCODE_GE] = &&label_ge, [REDUCT_OPCODE_GE_CONST] = &&label_ge_k,
        [REDUCT_OPCODE_ADD] = &&label_add, [REDUCT_OPCODE_ADD_CONST] = &&label_add_k,
        [REDUCT_OPCODE_SUB] = &&label_sub, [REDUCT_OPCODE_SUB_CONST] = &&label_sub_k,
        [REDUCT_OPCODE_MUL] = &&label_mul, [REDUCT_OPCODE_MUL_CONST] = &&label_mul_k,
        [REDUCT_OPCODE_DIV] = &&label_div, [REDUCT_OPCODE_DIV_CONST] = &&label_div_k,
        [REDUCT_OPCODE_MOD] = &&label_mod, [REDUCT_OPCODE_MOD_CONST] = &&label_mod_k,
        [REDUCT_OPCODE_BAND] = &&label_band, [REDUCT_OPCODE_BAND_CONST] = &&label_band_k,
        [REDUCT_OPCODE_BOR] = &&label_bor, [REDUCT_OPCODE_BOR_CONST] = &&label_bor_k,
        [REDUCT_OPCODE_BXOR] = &&label_bxor, [REDUCT_OPCODE_BXOR_CONST] = &&label_bxor_k,
        [REDUCT_OPCODE_BNOT] = &&label_bnot, [REDUCT_OPCODE_BNOT_CONST] = &&label_bnot_k,
        [REDUCT_OPCODE_SHL] = &&label_shl, [REDUCT_OPCODE_SHL_CONST] = &&label_shl_k,
        [REDUCT_OPCODE_SHR] = &&label_shr, [REDUCT_OPCODE_SHR_CONST] = &&label_shr_k,
        [REDUCT_OPCODE_JEQ] = &&label_jeq, [REDUCT_OPCODE_JEQ_CONST] = &&label_jeq_k,
        [REDUCT_OPCODE_JNEQ] = &&label_jneq, [REDUCT_OPCODE_JNEQ_CONST] = &&label_jneq_k,
        [REDUCT_OPCODE_JLT] = &&label_jlt, [REDUCT_OPCODE_JLT_CONST] = &&label_jlt_k,
        [REDUCT_OPCODE_JLE] = &&label_jle, [REDUCT_OPCODE_JLE_CONST] = &&label_jle_k,
        [REDUCT_OPCODE_JGT] = &&label_jgt, [REDUCT_OPCODE_JGT_CONST] = &&label_jgt_k,
        [REDUCT_OPCODE_JGE] = &&label_jge, [REDUCT_OPCODE_JGE_CONST] = &&label_jge_k,
        [REDUCT_OPCODE_LEN] = &&label_len, [REDUCT_OPCODE_LEN_CONST] = &&label_len_k,
        [REDUCT_OPCODE_NTH2] = &&label_nth2, [REDUCT_OPCODE_NTH2_CONST] = &&label_nth2_k,
        [REDUCT_OPCODE_NTH3] = &&label_nth3, [REDUCT_OPCODE_NTH3_CONST] = &&label_nth3_k,
        [REDUCT_OPCODE_RANGE1] = &&label_range1, [REDUCT_OPCODE_RANGE1_CONST] = &&label_range1_k,
        [REDUCT_OPCODE_RANGE2] = &&label_range2, [REDUCT_OPCODE_RANGE2_CONST] = &&label_range2_k,
        [REDUCT_OPCODE_RANGE3] = &&label_range3, [REDUCT_OPCODE_RANGE3_CONST] = &&label_range3_k,
        [REDUCT_OPCODE_FORK] = shouldFork ? &&label_fork : &&label_call, [REDUCT_OPCODE_FORK_CONST] = shouldFork ? &&label_fork_k : &&label_call_k,
        [REDUCT_OPCODE_JOIN] = shouldFork ? &&label_join : &&label_mov, [REDUCT_OPCODE_JOIN_CONST] = shouldFork ? &&label_join_k : &&label_mov_k,
    };

#define LABEL_C_OP(_label, ...) \
    _label: \
    { \
        DECODE_C_REG(); \
        __VA_ARGS__ \
    } \
    _label##_k: \
    { \
        DECODE_C_CONST(); \
        __VA_ARGS__ \
    }

    DISPATCH();
label_invalid:
{
    REDUCT_ERROR_THROW(reduct, "invalid opcode 0x%02X", REDUCT_INST_GET_OP(inst));
}
label_nop:
{
    DISPATCH();
}
label_list:
{
    DECODE_A();
    DECODE_B();
    r[a] = REDUCT_HANDLE_CREATE_HANDLES(reduct, b, &r[a]);
    DISPATCH();
}
label_jmp:
{
    DECODE_SAX();
    ip += sax;
    DISPATCH();
}
label_jmpf:
{
    DECODE_C();
    DECODE_SAX();
    reduct_handle_t val = r[c];
    if (!REDUCT_HANDLE_IS_TRUTHY(val))
    {
        ip += sax;
    }
    DISPATCH();
}
label_jmpt:
{
    DECODE_C();
    DECODE_SAX();
    reduct_handle_t val = r[c];
    if (REDUCT_HANDLE_IS_TRUTHY(val))
    {
        ip += sax;
    }
    DISPATCH();
}
LABEL_C_OP(label_call, {
    DECODE_A();
    DECODE_B();
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_CLOSURE(valC)))
    {
        reduct_closure_t* closure = REDUCT_HANDLE_TO_CLOSURE(valC);
        b = reduct_eval_bundle_args(reduct, closure->function, b, &r[a]);

        reduct_eval_push_frame(reduct, closure, frame->base + a);
        UPDATE_STATE();

        DISPATCH();
    }
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NATIVE(reduct, valC)))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(valC);

        assert(atom->native != NULL);
        reduct_handle_t res = atom->native(reduct, b, &r[a]);

        UPDATE_STATE();
        r[a] = res;

        REDUCT_GC_CHECK(reduct);
        DISPATCH();
    }

    REDUCT_ERROR_THROW(reduct, "cannot call value of type %s", REDUCT_HANDLE_GET_TYPE_STRING(valC));
})
LABEL_C_OP(label_tailcall, {
    DECODE_A();
    DECODE_B();
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_CLOSURE(valC)))
    {
        reduct_closure_t* closure = REDUCT_HANDLE_TO_CLOSURE(valC);
        b = reduct_eval_bundle_args(reduct, closure->function, b, &r[a]);

        if (REDUCT_LIKELY(a != 0))
        {
            reduct_handle_t* src = r + a;
            for (uint32_t i = 0; i < b; i++)
            {
                r[i] = src[i];
            }
        }

        reduct_eval_tail_frame(reduct, closure);

        UPDATE_STATE();

        DISPATCH();
    }
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NATIVE(reduct, valC)))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(valC);

        assert(atom->native != NULL);
        reduct_handle_t res = atom->native(reduct, b, &r[a]);

        frame = &reduct->eval.frames[reduct->eval.frameCount - 1];
        reduct->eval.regs[frame->base] = res;
        reduct_eval_pop_frame(reduct);

        if (REDUCT_UNLIKELY(reduct->eval.frameCount == initialFrameCount))
        {
            result = res;
            goto eval_end;
        }

        UPDATE_STATE();

        REDUCT_GC_CHECK(reduct);
        DISPATCH();
    }

    REDUCT_ERROR_THROW(reduct, "cannot call value of type %s", REDUCT_HANDLE_GET_TYPE_STRING(valC));
})
label_recur:
{
    DECODE_A();
    DECODE_B();
    reduct_closure_t* closure = frame->closure;
    b = reduct_eval_bundle_args(reduct, closure->function, b, &r[a]);

    reduct_eval_push_frame(reduct, closure, frame->base + a);

    UPDATE_STATE();
    REDUCT_GC_CHECK(reduct);
    DISPATCH();
}
label_tailrecur:
{
    DECODE_A();
    DECODE_B();
    reduct_closure_t* closure = frame->closure;
    b = reduct_eval_bundle_args(reduct, closure->function, b, &r[a]);
    if (REDUCT_LIKELY(a != 0))
    {
        reduct_handle_t* src = r + a;
        for (uint32_t i = 0; i < b; i++)
        {
            r[i] = src[i];
        }
    }
    frame->ip = closure->function->insts;
    ip = frame->ip;
    REDUCT_GC_CHECK(reduct);
    DISPATCH();
}
LABEL_C_OP(label_mov, {
    DECODE_A();
    r[a] = valC;
    DISPATCH();
})
LABEL_C_OP(label_ret, {
    reduct->eval.regs[frame->base] = valC;
    reduct_eval_pop_frame(reduct);
    if (REDUCT_UNLIKELY(reduct->eval.frameCount == initialFrameCount))
    {
        result = valC;
        REDUCT_GC_CHECK(reduct);
        goto eval_end;
    }
    UPDATE_STATE();
    REDUCT_GC_CHECK(reduct);
    DISPATCH();
})
OP_COMPARE(label_eq, ==)
OP_COMPARE(label_neq, !=)
OP_COMPARE(label_lt, <)
OP_COMPARE(label_le, <=)
OP_COMPARE(label_gt, >)
OP_COMPARE(label_ge, >=)
OP_SKIP_CMP(label_jeq, ==)
OP_SKIP_CMP(label_jneq, !=)
OP_SKIP_CMP(label_jlt, <)
OP_SKIP_CMP(label_jle, <=)
OP_SKIP_CMP(label_jgt, >)
OP_SKIP_CMP(label_jge, >=)
OP_ARITH(label_add, +)
OP_ARITH(label_sub, -)
OP_ARITH(label_mul, *)
LABEL_C_OP(label_div, {
    DECODE_A();
    DECODE_B();
    REDUCT_HANDLE_DIV_FAST(reduct, &r[a], r[b], valC);
    DISPATCH();
})
LABEL_C_OP(label_mod, {
    DECODE_A();
    DECODE_B();
    REDUCT_HANDLE_MOD_FAST(reduct, &r[a], r[b], valC);
    DISPATCH();
})
OP_BITWISE(label_band, &)
OP_BITWISE(label_bor, |)
OP_BITWISE(label_bxor, ^)
LABEL_C_OP(label_bnot, {
    DECODE_A();
    int64_t val = reduct_handle_as_int(reduct, valC);
    r[a] = REDUCT_HANDLE_FROM_NUMBER((double)(~val));
    DISPATCH();
})
OP_SHIFT(label_shl, <<, "left")
OP_SHIFT(label_shr, >>, "right")
LABEL_C_OP(label_len, {
    DECODE_A();
    r[a] = REDUCT_HANDLE_FROM_NUMBER(reduct_handle_as_item(reduct, valC)->length);
    DISPATCH();
})
LABEL_C_OP(label_nth2, {
    DECODE_A();
    DECODE_B();
    r[a] = reduct_nth(reduct, r[b], valC, REDUCT_HANDLE_NIL(reduct));
    DISPATCH();
})
LABEL_C_OP(label_nth3, {
    DECODE_A();
    DECODE_B();
    r[a] = reduct_nth(reduct, r[a], r[b], valC);
    DISPATCH();
})
LABEL_C_OP(label_range1, {
    DECODE_A();
    r[a] = reduct_range(reduct, REDUCT_HANDLE_FROM_NUMBER(0.0), valC, REDUCT_HANDLE_NIL(reduct));
    DISPATCH();
})
LABEL_C_OP(label_range2, {
    DECODE_A();
    DECODE_B();
    r[a] = reduct_range(reduct, r[b], valC, REDUCT_HANDLE_NIL(reduct));
    DISPATCH();
})
LABEL_C_OP(label_range3, {
    DECODE_A();
    DECODE_B();
    r[a] = reduct_range(reduct, r[a], r[b], valC);
    DISPATCH();
})
LABEL_C_OP(label_fork, {
    DECODE_A();
    DECODE_B();
    reduct_handle_t future = REDUCT_HANDLE_CREATE_FUTURE(reduct, valC, b, &r[a]);
    UPDATE_STATE();
    r[a] = future;
    DISPATCH();
})
LABEL_C_OP(label_join, {
    DECODE_A();
    reduct_handle_t res;
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_FUTURE(valC)))
    {
        reduct_future_t* future = REDUCT_HANDLE_TO_FUTURE(valC);
        res = reduct_future_join(reduct, future);
    }
    else
    {
        res = valC;
    }
    UPDATE_STATE();
    r[a] = res;
    REDUCT_GC_CHECK(reduct);
    DISPATCH();
})
label_closure:
{
    DECODE_A();
    DECODE_C();
    reduct_handle_t protoHandle = k[c];
    assert(REDUCT_HANDLE_IS_ITEM(protoHandle));

    reduct_item_t* protoItem = REDUCT_HANDLE_TO_ITEM(protoHandle);
    assert(protoItem->type == REDUCT_ITEM_TYPE_FUNCTION);

    reduct_function_t* proto = &protoItem->function;
    r[a] = REDUCT_HANDLE_FROM_CLOSURE(reduct_closure_new(reduct, proto));
    DISPATCH();
}
LABEL_C_OP(label_capture, {
    DECODE_A();
    DECODE_B();
    reduct_closure_t* closurePtr = &REDUCT_HANDLE_TO_ITEM(r[a])->closure;
    closurePtr->constants[b] = valC;
    DISPATCH();
})
eval_end:
    // clang-format on
    return result;
}

REDUCT_API reduct_handle_t reduct_eval(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        reduct_handle_t graph = reduct_build(reduct, handle);
        reduct_optimize(reduct, graph, reduct->global->optimize.lastFlags);
        reduct_handle_t function = reduct_emit(reduct, graph);
        return reduct_eval(reduct, function);
    }

    reduct_function_t* function = REDUCT_HANDLE_TO_FUNCTION(handle);
    reduct_eval_ensure_ready(reduct);

    reduct_closure_t* closure = reduct_closure_new(reduct, function);
    uint32_t initialFrameCount = reduct->eval.frameCount;

    reduct_eval_push_frame(reduct, closure, reduct->eval.regCount);

    return reduct_eval_run(reduct, initialFrameCount);
}

REDUCT_API reduct_handle_t reduct_eval_file(reduct_t* reduct, const char* path, reduct_optimize_flags_t optimize)
{
    assert(reduct != NULL);
    assert(path != NULL);

    reduct_handle_t ast = reduct_parse_file(reduct, path);
    reduct_handle_t graph = reduct_build(reduct, ast);
    reduct_optimize(reduct, graph, optimize);
    reduct_handle_t function = reduct_emit(reduct, graph);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_string(reduct_t* reduct, const char* str, size_t len,
    reduct_optimize_flags_t optimize)
{
    assert(reduct != NULL);
    assert(str != NULL);

    reduct_handle_t ast = reduct_parse(reduct, str, len, "<eval>");
    reduct_handle_t graph = reduct_build(reduct, ast);
    reduct_optimize(reduct, graph, optimize);
    reduct_handle_t function = reduct_emit(reduct, graph);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_call(reduct_t* reduct, reduct_handle_t callable, size_t argc,
    reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    reduct_eval_ensure_ready(reduct);

    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_CLOSURE(callable)))
    {
        reduct_closure_t* closure = REDUCT_HANDLE_TO_CLOSURE(callable);
        reduct_function_t* func = closure->function;
        uint32_t arity = (func->flags & REDUCT_FUNCTION_FLAG_VARIADIC) ? func->arity : (uint32_t)argc;
        uint32_t target = reduct->eval.regCount;
        uint32_t needed = REDUCT_MAX(arity, (uint32_t)argc);
        needed = REDUCT_MAX(needed, (uint32_t)func->registerCount);

        if (REDUCT_UNLIKELY(target + needed > reduct->eval.regCapacity))
        {
            bool argvInRegs =
                (argv != NULL && argv >= reduct->eval.regs && argv < reduct->eval.regs + reduct->eval.regCapacity);
            uint32_t argvOffset = argvInRegs ? (uint32_t)(argv - reduct->eval.regs) : 0;
            reduct_eval_ensure_regs(reduct, target + needed);

            if (argvInRegs)
            {
                argv = reduct->eval.regs + argvOffset;
            }
        }

        reduct->eval.regCount = target + needed;
        if (argc > 0)
        {
            memmove(reduct->eval.regs + target, argv, argc * sizeof(reduct_handle_t));
        }
        reduct_eval_bundle_args(reduct, func, (uint32_t)argc, &reduct->eval.regs[target]);

        uint32_t initialFrameCount = reduct->eval.frameCount;
        reduct_eval_push_frame(reduct, closure, target);

        return reduct_eval_run(reduct, initialFrameCount);
    }

    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NATIVE(reduct, callable)))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(callable);

        assert(atom->native != NULL);
        return atom->native(reduct, argc, argv);
    }

    REDUCT_ERROR_THROW(reduct, "cannot call value of type %s", REDUCT_HANDLE_GET_TYPE_STRING(callable));
}

REDUCT_API reduct_handle_t reduct_eval_call_v(struct reduct* reduct, reduct_handle_t callable, size_t argc, ...)
{
    assert(reduct != NULL);

    if (argc == 0)
    {
        return reduct_eval_call(reduct, callable, 0, NULL);
    }

    REDUCT_SCRATCH_GET(reduct, argv, reduct_handle_t, argc);
    va_list args;
    va_start(args, argc);
    for (size_t i = 0; i < argc; i++)
    {
        argv[i] = va_arg(args, reduct_handle_t);
    }
    va_end(args);

    reduct_handle_t result = reduct_eval_call(reduct, callable, argc, argv);
    REDUCT_SCRATCH_PUT(reduct, argv);
    return result;
}
