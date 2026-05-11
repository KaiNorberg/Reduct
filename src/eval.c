#include "reduct/closure.h"
#include "reduct/defs.h"
#include "reduct/eval.h"
#include "reduct/item.h"
#include "reduct/standard.h"
#include "reduct/parse.h"
#include "reduct/compile.h"

static inline REDUCT_ALWAYS_INLINE void reduct_eval_ensure_regs(reduct_t* reduct,
    uint32_t neededRegs)
{
    assert(reduct != NULL);

    if (REDUCT_LIKELY(neededRegs <= reduct->regCapacity))
    {
        return;
    }

    uint32_t oldCapacity = reduct->regCapacity;
    while (neededRegs > reduct->regCapacity)
    {
        reduct->regCapacity *= REDUCT_EVAL_REGS_GROWTH_FACTOR;
    }
    if (REDUCT_UNLIKELY(reduct->regCapacity > REDUCT_EVAL_REGS_MAX))
    {
        REDUCT_ERROR_INTERNAL(reduct, "too many registers");
    }

    reduct_handle_t* newRegs = (reduct_handle_t*)realloc(reduct->regs, sizeof(reduct_handle_t) * reduct->regCapacity);
    if (newRegs == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memset(newRegs + oldCapacity, 0, (reduct->regCapacity - oldCapacity) * sizeof(reduct_handle_t));
    reduct->regs = newRegs;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_push_frame(reduct_t* reduct,
    reduct_closure_t* closure, uint32_t target)
{
    assert(reduct != NULL);
    assert(closure != NULL);

    if (REDUCT_UNLIKELY(reduct->frameCount >= reduct->frameCapacity))
    {
        if (REDUCT_UNLIKELY(reduct->frameCapacity * REDUCT_EVAL_FRAMES_GROWTH_FACTOR >= REDUCT_EVAL_FRAMES_MAX))
        {
            REDUCT_ERROR_INTERNAL(reduct, "stack overflow");
        }

        reduct->frameCapacity *= REDUCT_EVAL_FRAMES_GROWTH_FACTOR;
        reduct->frames = (reduct_eval_frame_t*)realloc(reduct->frames,
            sizeof(reduct_eval_frame_t) * reduct->frameCapacity);
        if (reduct->frames == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    uint32_t neededRegs = target + closure->function->registerCount;
    reduct_eval_ensure_regs(reduct, neededRegs);

    reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->insts;
    frame->base = target;
    frame->prevRegCount = reduct->regCount;

    reduct->regCount = neededRegs;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_pop_frame(reduct_t* reduct)
{
    assert(reduct->frameCount > 0);

    reduct_eval_frame_t* frame = &reduct->frames[--reduct->frameCount];

    reduct->regCount = frame->prevRegCount;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_tail_frame(reduct_t* reduct,
    reduct_closure_t* closure)
{
    assert(reduct != NULL);
    assert(reduct->frameCount > 0);
    assert(closure != NULL);

    reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount - 1];

    uint32_t neededRegs = frame->base + closure->function->registerCount;
    reduct_eval_ensure_regs(reduct, neededRegs);
    reduct->regCount = neededRegs;

    frame->closure = closure;
    frame->ip = closure->function->insts;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_ensure_ready(reduct_t* reduct)
{
    if (REDUCT_UNLIKELY(reduct->frameCapacity == 0))
    {
        reduct->frameCapacity = REDUCT_EVAL_FRAMES_INITIAL;
        reduct->frames = (reduct_eval_frame_t*)calloc(1, sizeof(reduct_eval_frame_t) * reduct->frameCapacity);
        if (reduct->frames == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    if (REDUCT_UNLIKELY(reduct->regCapacity == 0))
    {
        reduct->regCapacity = REDUCT_EVAL_REGS_INITIAL;
        reduct->regs = (reduct_handle_t*)calloc(1, sizeof(reduct_handle_t) * reduct->regCapacity);
        if (reduct->regs == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
}

static inline REDUCT_ALWAYS_INLINE uint32_t reduct_eval_bundle_args(reduct_t* reduct, reduct_function_t* func,
    uint32_t argc, reduct_handle_t* argv)
{
    uint32_t arity = func->arity;
    if (func->flags & REDUCT_FUNCTION_FLAG_VARIADIC)
    {
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= (uint32_t)arity - 1, NULL,
            "expected at least %u arguments, got %u", arity - 1, argc);

        uint32_t fixed = arity - 1;
        argv[fixed] = REDUCT_HANDLE_CREATE_HANDLES(reduct, argc - fixed, &argv[fixed]);
        return arity;
    }
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == arity, NULL, "expected %u arguments, got %u", arity, argc);
    return argc;
}

static inline reduct_handle_t reduct_eval_run(reduct_t* reduct, uint32_t initialFrameCount)
{
    assert(reduct != NULL);

    reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount - 1];
    reduct_inst_t* ip = frame->ip;
    reduct_handle_t* base = reduct->regs + frame->base;
    reduct_handle_t* constants = frame->closure->constants;
    reduct_inst_t inst;
    reduct_handle_t result = REDUCT_HANDLE_NONE;

    /// @todo Write some macro magic to turn this into a switch case if not using GCC or Clang.

    // The `frame->ip = ip` line is needed for error checking, even if it does hurt performance.
#define DISPATCH() \
    do \
    { \
        inst = *ip++; \
        frame->ip = ip; \
        reduct_opcode_t op = REDUCT_INST_GET_OP(inst); \
        goto* dispatchTable[op]; \
    } while (0)

#define UPDATE_STATE() \
    do { \
        frame = &reduct->frames[reduct->frameCount - 1]; \
        ip = frame->ip; \
        base = reduct->regs + frame->base; \
        constants = frame->closure->constants; \
    } while (0)

#define PREPARE_CALLABLE() \
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ITEM(&valC), NULL, "cannot call value of type %s", \
        REDUCT_HANDLE_GET_TYPE_STR(&valC)); \
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&valC)

#define DECODE_A() uint32_t a = REDUCT_INST_GET_A(inst)
#define DECODE_B() uint32_t b = REDUCT_INST_GET_B(inst)
#define DECODE_C() uint32_t c = REDUCT_INST_GET_C(inst)
#define DECODE_C_REG() \
    uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = base[c]
#define DECODE_C_CONST() \
    uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = constants[c]
#define DECODE_SBX() int32_t sbx = REDUCT_INST_GET_SBX(inst)

#define OP_ENTRY(_op, _label) [_op] = &&_label, [_op | REDUCT_MODE_CONST] = &&_label

#define OP_ENTRY_C(_op, _label) [_op] = &&_label, [_op | REDUCT_MODE_CONST] = &&_label##_k

#define OP_BITWISE(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    REDUCT_HANDLE_BITWISE_FAST(reduct, &base[a], &base[b], &valC, _op); \
    DISPATCH(); \
})

#define OP_EQUALITY(_label, _func, _truth) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    base[a] = REDUCT_HANDLE_FROM_BOOL(_func(reduct, &base[b], &valC) == _truth); \
    DISPATCH(); \
})

#define OP_COMPARE(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    base[a] = REDUCT_HANDLE_FROM_BOOL(REDUCT_HANDLE_COMPARE_FAST(reduct, &base[b], &valC, _op)); \
    DISPATCH(); \
})

#define OP_ARITH(_label, _op) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    REDUCT_HANDLE_ARITHMETIC_FAST(reduct, &base[a], &base[b], &valC, _op); \
    DISPATCH(); \
})

#define OP_SHIFT(_label, _op, _name) \
LABEL_C_OP(_label, { \
    DECODE_A(); \
    DECODE_B(); \
    int64_t amount; \
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&valC))) \
    { \
        amount = REDUCT_HANDLE_TO_INT(&valC); \
    } \
    else \
    { \
        amount = reduct_handle_as_int(reduct, &valC); \
    } \
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, amount >= 0 && amount < REDUCT_HANDLE_INT_WIDTH - 1, _name " shift amount must be 0-%ld, got %ld", REDUCT_HANDLE_INT_WIDTH - 1, amount); \
    base[a] = REDUCT_HANDLE_FROM_INT(reduct_handle_as_int(reduct, &base[b]) _op amount); \
    DISPATCH(); \
})

    static const void* dispatchTable[] = {
        OP_ENTRY(REDUCT_OPCODE_NONE, label_none),
        OP_ENTRY(REDUCT_OPCODE_LIST, label_list),
        OP_ENTRY(REDUCT_OPCODE_JMP, label_jmp),
        OP_ENTRY(REDUCT_OPCODE_JMPF, label_jmpf),
        OP_ENTRY(REDUCT_OPCODE_JMPT, label_jmpt),
        OP_ENTRY_C(REDUCT_OPCODE_CALL, label_call),
        OP_ENTRY_C(REDUCT_OPCODE_MOV, label_mov),
        OP_ENTRY_C(REDUCT_OPCODE_RET, label_ret),
        OP_ENTRY_C(REDUCT_OPCODE_EQ, label_eq),
        OP_ENTRY_C(REDUCT_OPCODE_NEQ, label_neq),
        OP_ENTRY_C(REDUCT_OPCODE_SEQ, label_seq),
        OP_ENTRY_C(REDUCT_OPCODE_SNEQ, label_sneq),
        OP_ENTRY_C(REDUCT_OPCODE_LT, label_lt),
        OP_ENTRY_C(REDUCT_OPCODE_LE, label_le),
        OP_ENTRY_C(REDUCT_OPCODE_GT, label_gt),
        OP_ENTRY_C(REDUCT_OPCODE_GE, label_ge),
        OP_ENTRY_C(REDUCT_OPCODE_ADD, label_add),
        OP_ENTRY_C(REDUCT_OPCODE_SUB, label_sub),
        OP_ENTRY_C(REDUCT_OPCODE_MUL, label_mul),
        OP_ENTRY_C(REDUCT_OPCODE_DIV, label_div),
        OP_ENTRY_C(REDUCT_OPCODE_MOD, label_mod),
        OP_ENTRY_C(REDUCT_OPCODE_BAND, label_band),
        OP_ENTRY_C(REDUCT_OPCODE_BOR, label_bor),
        OP_ENTRY_C(REDUCT_OPCODE_BXOR, label_bxor),
        OP_ENTRY_C(REDUCT_OPCODE_BNOT, label_bnot),
        OP_ENTRY_C(REDUCT_OPCODE_SHL, label_shl),
        OP_ENTRY_C(REDUCT_OPCODE_SHR, label_shr),
        OP_ENTRY(REDUCT_OPCODE_CLOSURE, label_closure),
        OP_ENTRY_C(REDUCT_OPCODE_CAPTURE, label_capture),
        OP_ENTRY_C(REDUCT_OPCODE_TAILCALL, label_tailcall),
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
label_none:
    REDUCT_ERROR_RUNTIME(reduct, "invalid opcode %u at instruction %zu", inst, (size_t)(ip - frame->closure->function->insts - 1));
label_list:
{
    DECODE_A();
    DECODE_B();
    base[a] = REDUCT_HANDLE_CREATE_HANDLES(reduct, b, &base[a]);
    DISPATCH();
}
label_jmp:
{
    DECODE_SBX();
    ip += sbx;
    DISPATCH();
}
label_jmpf:
{
    DECODE_A();
    DECODE_SBX();
    reduct_handle_t val = base[a];
    if (!REDUCT_HANDLE_IS_TRUTHY(&val))
    {
        ip += sbx;
    }
    DISPATCH();
}
label_jmpt:
{
    DECODE_A();
    DECODE_SBX();
    reduct_handle_t val = base[a];
    if (REDUCT_HANDLE_IS_TRUTHY(&val))
    {
        ip += sbx;
    }
    DISPATCH();
}
LABEL_C_OP(label_call, {
    DECODE_A();
    DECODE_B();
    PREPARE_CALLABLE();
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_CLOSURE))
    {
        reduct_closure_t* closure = &item->closure;
        b = reduct_eval_bundle_args(reduct, closure->function, b, &base[a]);

        reduct_eval_push_frame(reduct, closure, frame->base + a);

        UPDATE_STATE();

        DISPATCH();
    }
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_native(reduct, &item->atom)))
    {
        reduct_handle_t* args = &base[a];
        reduct_handle_t result = item->atom.native(reduct, b, args);
        UPDATE_STATE();
        base[a] = result;

        reduct_gc_if_needed(reduct);

        DISPATCH();
    }

    REDUCT_ERROR_RUNTIME(reduct, "cannot call value of type %s", reduct_item_type_str(item));
})
LABEL_C_OP(label_tailcall, {
    DECODE_A();
    DECODE_B();
    PREPARE_CALLABLE();
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_CLOSURE))
    {
        reduct_closure_t* closure = &item->closure;
        b = reduct_eval_bundle_args(reduct, closure->function, b, &base[a]);

        if (a != 0)
        {
            memmove(base, base + a, b * sizeof(reduct_handle_t));
        }

        reduct_eval_tail_frame(reduct, closure);

        UPDATE_STATE();

        DISPATCH();
    }
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_native(reduct, &item->atom)))
    {
        reduct_handle_t* args = &base[a];
        reduct_handle_t res = item->atom.native(reduct, b, args);

        frame = &reduct->frames[reduct->frameCount - 1];
        reduct->regs[frame->base] = res;
        reduct_eval_pop_frame(reduct);

        if (REDUCT_UNLIKELY(reduct->frameCount == initialFrameCount))
        {
            result = res;
            goto eval_end;
        }

        UPDATE_STATE();

        reduct_gc_if_needed(reduct);

        DISPATCH();
    }

    REDUCT_ERROR_RUNTIME(reduct, "cannot call value of type %s", reduct_item_type_str(item));
})
LABEL_C_OP(label_mov, {
    DECODE_A();
    base[a] = valC;
    DISPATCH();
})
LABEL_C_OP(label_ret, {
    reduct->regs[frame->base] = valC;
    reduct_eval_pop_frame(reduct);
    if (REDUCT_UNLIKELY(reduct->frameCount == initialFrameCount))
    {
        result = valC;
        goto eval_end;
    }
    UPDATE_STATE();
    DISPATCH();
})
OP_COMPARE(label_eq, ==)
OP_COMPARE(label_neq, !=)
OP_EQUALITY(label_seq, reduct_handle_is_equal, true)
OP_EQUALITY(label_sneq, reduct_handle_is_equal, false)
OP_COMPARE(label_lt, <)
OP_COMPARE(label_le, <=)
OP_COMPARE(label_gt, >)
OP_COMPARE(label_ge, >=)
OP_ARITH(label_add, +)
OP_ARITH(label_sub, -)
OP_ARITH(label_mul, *)
LABEL_C_OP(label_div, {
    DECODE_A();
    DECODE_B();
    REDUCT_HANDLE_DIV_FAST(reduct, &base[a], &base[b], &valC);
    DISPATCH();
})
LABEL_C_OP(label_mod, {
    DECODE_A();
    DECODE_B();
    REDUCT_HANDLE_MOD_FAST(reduct, &base[a], &base[b], &valC);
    DISPATCH();
})
OP_BITWISE(label_band, &)
OP_BITWISE(label_bor, |)
OP_BITWISE(label_bxor, ^)
LABEL_C_OP(label_bnot, {
    DECODE_A();
    int64_t val;
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&valC)))
    {
        val = REDUCT_HANDLE_TO_INT(&valC);
    }
    else
    {
        val = reduct_handle_as_int(reduct, &valC);
    }
    base[a] = REDUCT_HANDLE_FROM_INT(~val);
    DISPATCH();
})
OP_SHIFT(label_shl, <<, "left")
OP_SHIFT(label_shr, >>, "right")
label_closure:
{
    DECODE_A();
    DECODE_C();
    reduct_handle_t protoHandle = frame->closure->constants[c];
    assert(REDUCT_HANDLE_IS_ITEM(&protoHandle));

    reduct_item_t* protoItem = REDUCT_HANDLE_TO_ITEM(&protoHandle);
    assert(protoItem->type == REDUCT_ITEM_TYPE_FUNCTION);

    reduct_function_t* proto = &protoItem->function;
    base[a] = REDUCT_HANDLE_FROM_CLOSURE(reduct_closure_new(reduct, proto));
    DISPATCH();
}
LABEL_C_OP(label_capture, {
    DECODE_A();
    DECODE_B();
    reduct_handle_t closureHandle = base[a];
    reduct_closure_t* closurePtr = &REDUCT_HANDLE_TO_ITEM(&closureHandle)->closure;
    closurePtr->constants[b] = valC;
    DISPATCH();
})
eval_end:
    // clang-format on
    return result;
}

REDUCT_API reduct_handle_t reduct_eval(reduct_t* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    assert(function != NULL);

    reduct_eval_ensure_ready(reduct);

    reduct_closure_t* closure = reduct_closure_new(reduct, function);
    uint32_t initialFrameCount = reduct->frameCount;

    reduct_eval_push_frame(reduct, closure, reduct->regCount);

    return reduct_eval_run(reduct, initialFrameCount);
}

REDUCT_API reduct_handle_t reduct_eval_file(reduct_t* reduct, const char* path)
{
    assert(reduct != NULL);
    assert(path != NULL);

    reduct_handle_t parsed = reduct_parse_file(reduct, path);
    reduct_function_t* function = reduct_compile(reduct, &parsed);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_string(reduct_t* reduct, const char* str, size_t len)
{
    assert(reduct != NULL);
    assert(str != NULL);

    reduct_handle_t parsed = reduct_parse(reduct, str, len, "<eval>");
    reduct_function_t* function = reduct_compile(reduct, &parsed);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_call(reduct_t* reduct, reduct_handle_t callable, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ITEM(&callable), NULL, "cannot call value");

    reduct_eval_ensure_ready(reduct);

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&callable);
    if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, reduct_atom_is_native(reduct, &item->atom), NULL, "cannot call atom, only native functions and closures are callable");
        return item->atom.native(reduct, argc, argv);
    }

    if (item->type == REDUCT_ITEM_TYPE_CLOSURE)
    {
        reduct_closure_t* closure = &item->closure;
        reduct_function_t* func = closure->function;
        uint32_t arity = (func->flags & REDUCT_FUNCTION_FLAG_VARIADIC) ? func->arity : (uint32_t)argc;
        uint32_t target = reduct->regCount;
        uint32_t needed = REDUCT_MAX(arity, (uint32_t)argc);
        needed = REDUCT_MAX(needed, (uint32_t)func->registerCount);

        if (REDUCT_UNLIKELY(target + needed > reduct->regCapacity))
        {
            bool argvInRegs = (argv != NULL && argv >= reduct->regs && argv < reduct->regs + reduct->regCapacity);
            uint32_t argvOffset = argvInRegs ? (uint32_t)(argv - reduct->regs) : 0;
            reduct_eval_ensure_regs(reduct, target + needed);

            if (argvInRegs)
            {
                argv = reduct->regs + argvOffset;
            }
        }

        if (argc > 0)
        {
            memcpy(reduct->regs + target, argv, argc * sizeof(reduct_handle_t));
        }
        reduct_eval_bundle_args(reduct, func, (uint32_t)argc, &reduct->regs[target]);

        uint32_t initialFrameCount = reduct->frameCount;
        reduct_eval_push_frame(reduct, closure, target);

        return reduct_eval_run(reduct, initialFrameCount);
    }

    REDUCT_ERROR_RUNTIME(reduct, "cannot call value");
}
