#include "reduct/closure.h"
#include "reduct/defs.h"
#include "reduct/eval.h"
#include "reduct/item.h"
#include "reduct/standard.h"
#include "reduct/parse.h"
#include "reduct/compile.h"

static inline REDUCT_ALWAYS_INLINE void reduct_eval_ensure_regs(reduct_t* reduct,
    reduct_uint32_t neededRegs)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (REDUCT_LIKELY(neededRegs <= reduct->regCapacity))
    {
        return;
    }

    reduct_uint32_t oldCapacity = reduct->regCapacity;
    while (neededRegs > reduct->regCapacity)
    {
        reduct->regCapacity *= REDUCT_EVAL_REGS_GROWTH_FACTOR;
    }
    reduct_handle_t* newRegs = (reduct_handle_t*)REDUCT_REALLOC(reduct->regs, sizeof(reduct_handle_t) * reduct->regCapacity);
    if (newRegs == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    REDUCT_MEMSET(newRegs + oldCapacity, 0, (reduct->regCapacity - oldCapacity) * sizeof(reduct_handle_t));
    reduct->regs = newRegs;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_push_frame(reduct_t* reduct,
    reduct_closure_t* closure, reduct_uint32_t target)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(closure != REDUCT_NULL);

    if (REDUCT_UNLIKELY(reduct->frameCount >= reduct->frameCapacity))
    {
        reduct->frameCapacity *= REDUCT_EVAL_FRAMES_GROWTH_FACTOR;
        reduct_eval_frame_t* newFrames = (reduct_eval_frame_t*)REDUCT_REALLOC(reduct->frames,
                sizeof(reduct_eval_frame_t) * reduct->frameCapacity);
        if (newFrames == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->frames = newFrames;
    }

    reduct_uint32_t neededRegs = target + closure->function->registerCount;
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
    REDUCT_ASSERT(reduct->frameCount > 0);

    reduct_eval_frame_t* frame = &reduct->frames[--reduct->frameCount];

    reduct->regCount = frame->prevRegCount;
}

static inline REDUCT_ALWAYS_INLINE void reduct_eval_tail_frame(reduct_t* reduct,
    reduct_closure_t* closure)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(reduct->frameCount > 0);
    REDUCT_ASSERT(closure != REDUCT_NULL);

    reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount - 1];

    reduct_uint32_t neededRegs = frame->base + closure->function->registerCount;
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
        reduct->frames = (reduct_eval_frame_t*)REDUCT_CALLOC(1, sizeof(reduct_eval_frame_t) * reduct->frameCapacity);
        if (reduct->frames == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    if (REDUCT_UNLIKELY(reduct->regCapacity == 0))
    {
        reduct->regCapacity = REDUCT_EVAL_REGS_INITIAL;
        reduct->regs = (reduct_handle_t*)REDUCT_CALLOC(1, sizeof(reduct_handle_t) * reduct->regCapacity);
        if (reduct->regs == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
}

static inline reduct_handle_t reduct_eval_run(reduct_t* reduct, reduct_uint32_t initialFrameCount)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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

#define DECODE_A() reduct_uint32_t a = REDUCT_INST_GET_A(inst)
#define DECODE_B() reduct_uint32_t b = REDUCT_INST_GET_B(inst)
#define DECODE_C() reduct_uint32_t c = REDUCT_INST_GET_C(inst)
#define DECODE_C_REG() \
    reduct_uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = base[c]
#define DECODE_C_CONST() \
    reduct_uint32_t c = REDUCT_INST_GET_C(inst); \
    reduct_handle_t valC = constants[c]
#define DECODE_SBX() reduct_int32_t sbx = REDUCT_INST_GET_SBX(inst)

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
    REDUCT_ERROR_RUNTIME(reduct, "invalid opcode %u at instruction %zu", inst, (reduct_size_t)(ip - frame->closure->function->insts - 1));
label_list:
{
    DECODE_A();
    DECODE_B();
    base[a] = REDUCT_HANDLE_HANDLES(reduct, b, &base[a]);
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
    if (REDUCT_LIKELY(val == REDUCT_HANDLE_FALSE()) ||
        REDUCT_UNLIKELY(val != REDUCT_HANDLE_TRUE() && !REDUCT_HANDLE_IS_TRUTHY(&val)))
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
    if (REDUCT_LIKELY(val == REDUCT_HANDLE_TRUE()) ||
        REDUCT_UNLIKELY(val != REDUCT_HANDLE_FALSE() && REDUCT_HANDLE_IS_TRUTHY(&val)))
    {
        ip += sbx;
    }
    DISPATCH();
}
LABEL_C_OP(label_call, {
    DECODE_A();
    DECODE_B();
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ITEM(&valC), REDUCT_NULL, "cannot call value of type %s",
        REDUCT_HANDLE_GET_TYPE_STR(&valC));
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&valC);
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_CLOSURE))
    {
        reduct_closure_t* closure = &item->closure;
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, b == closure->function->arity, REDUCT_NULL, "expected %d arguments, got %d",
            closure->function->arity, b);

        reduct_eval_push_frame(reduct, closure, frame->base + a);

        frame = &reduct->frames[reduct->frameCount - 1];
        ip = frame->ip;
        base = reduct->regs + frame->base;
        constants = frame->closure->constants;

        DISPATCH();
    }
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_native(reduct, &item->atom)))
    {
        reduct_handle_t* args = &base[a];
        reduct_handle_t result = item->atom.native(reduct, b, args);

        frame = &reduct->frames[reduct->frameCount - 1];
        base = reduct->regs + frame->base;
        base[a] = result;
        constants = frame->closure->constants;

        reduct_gc_if_needed(reduct);

        DISPATCH();
    }

    REDUCT_ERROR_RUNTIME(reduct, "cannot call value of type %s", reduct_item_type_str(item));
})
LABEL_C_OP(label_tailcall, {
    DECODE_A();
    DECODE_B();
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ITEM(&valC), REDUCT_NULL, "cannot call value of type %s",
        REDUCT_HANDLE_GET_TYPE_STR(&valC));
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&valC);
    if (REDUCT_LIKELY(item->type == REDUCT_ITEM_TYPE_CLOSURE))
    {
        reduct_closure_t* closure = &item->closure;
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, b == closure->function->arity, REDUCT_NULL, "expected %d arguments, got %d",
            closure->function->arity, b);

        if (a != 0)
        {
            REDUCT_MEMMOVE(base, base + a, b * sizeof(reduct_handle_t));
        }

        reduct_eval_tail_frame(reduct, closure);

        frame = &reduct->frames[reduct->frameCount - 1];
        ip = frame->ip;
        base = reduct->regs + frame->base;
        constants = frame->closure->constants;

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

        frame = &reduct->frames[reduct->frameCount - 1];
        ip = frame->ip;
        base = reduct->regs + frame->base;
        constants = frame->closure->constants;

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
    frame = &reduct->frames[reduct->frameCount - 1];
    ip = frame->ip;
    base = reduct->regs + frame->base;
    constants = frame->closure->constants;
    DISPATCH();
})
OP_COMPARE(label_eq, ==)
OP_COMPARE(label_neq, !=)
OP_EQUALITY(label_seq, reduct_handle_is_equal, REDUCT_TRUE)
OP_EQUALITY(label_sneq, reduct_handle_is_equal, REDUCT_FALSE)
OP_COMPARE(label_lt, <)
OP_COMPARE(label_le, <=)
OP_COMPARE(label_gt, >)
OP_COMPARE(label_ge, >=)
OP_ARITH(label_add, +)
OP_ARITH(label_sub, -)
OP_ARITH(label_mul, *)
OP_ARITH(label_div, /)
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
    reduct_int64_t val;
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&valC)))
    {
        val = REDUCT_HANDLE_TO_INT(&valC);
    }
    else
    {
        val = reduct_get_int(reduct, &valC);
    }
    base[a] = REDUCT_HANDLE_FROM_INT(~val);
    DISPATCH();
})
LABEL_C_OP(label_shl, {
    DECODE_A();
    DECODE_B();
    reduct_int64_t left;
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&valC)))
    {
        left = REDUCT_HANDLE_TO_INT(&valC);
    }
    else
    {
        left = reduct_get_int(reduct, &valC);
    }
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, left >= 0 && left < 64, "left shift amount must be 0-63, got %ld", left);
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&base[b])))
    {
        base[a] = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&base[b]) << left);
    }
    else
    {
        base[a] = REDUCT_HANDLE_FROM_INT(reduct_get_int(reduct, &base[b]) << left);
    }
    DISPATCH();
})
LABEL_C_OP(label_shr, {
    DECODE_A();
    DECODE_B();
    reduct_int64_t right;
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&valC)))
    {
        right = REDUCT_HANDLE_TO_INT(&valC);
    }
    else
    {
        right = reduct_get_int(reduct, &valC);
    }
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, right >= 0 && right < 64, "right shift amount must be 0-63, got %ld", right);
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_INT(&base[b])))
    {
        base[a] = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&base[b]) >> right);
    }
    else
    {
        base[a] = REDUCT_HANDLE_FROM_INT(reduct_get_int(reduct, &base[b]) >> right);
    }
    DISPATCH();
})
label_closure:
{
    DECODE_A();
    DECODE_C();
    reduct_handle_t protoHandle = frame->closure->constants[c];
    REDUCT_ASSERT(REDUCT_HANDLE_IS_ITEM(&protoHandle));

    reduct_item_t* protoItem = REDUCT_HANDLE_TO_ITEM(&protoHandle);
    REDUCT_ASSERT(protoItem->type == REDUCT_ITEM_TYPE_FUNCTION);

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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(function != REDUCT_NULL);

    reduct_eval_ensure_ready(reduct);

    reduct_closure_t* closure = reduct_closure_new(reduct, function);
    reduct_uint32_t initialFrameCount = reduct->frameCount;

    reduct_eval_push_frame(reduct, closure, reduct->regCount);

    return reduct_eval_run(reduct, initialFrameCount);
}

REDUCT_API reduct_handle_t reduct_eval_file(reduct_t* reduct, const char* path)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(path != REDUCT_NULL);

    reduct_handle_t parsed = reduct_parse_file(reduct, path);
    reduct_function_t* function = reduct_compile(reduct, &parsed);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_string(reduct_t* reduct, const char* str, reduct_size_t len)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(str != REDUCT_NULL);

    reduct_handle_t parsed = reduct_parse(reduct, str, len, "<eval>");
    reduct_function_t* function = reduct_compile(reduct, &parsed);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_eval_call(reduct_t* reduct, reduct_handle_t callable, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ITEM(&callable), REDUCT_NULL, "cannot call value");

    reduct_eval_ensure_ready(reduct);

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&callable);
    if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, reduct_atom_is_native(reduct, &item->atom), REDUCT_NULL, "cannot call atom, only native functions and closures are callable");
        return item->atom.native(reduct, argc, argv);
    }

    if (item->type == REDUCT_ITEM_TYPE_CLOSURE)
    {
        reduct_closure_t* closure = &item->closure;
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == closure->function->arity, REDUCT_NULL, "expected %ld arguments, got %ld", closure->function->arity, argc);

        reduct_uint32_t target = reduct->regCount;

        if (REDUCT_UNLIKELY(target + argc > reduct->regCapacity))
        {
            reduct_bool_t argvInRegs = (argv >= reduct->regs && argv < reduct->regs + reduct->regCapacity);
            reduct_uint32_t argvOffset = argvInRegs ? (reduct_uint32_t)(argv - reduct->regs) : 0;

            reduct_eval_ensure_regs(reduct, target + argc);

            if (argvInRegs)
            {
                argv = reduct->regs + argvOffset;
            }
        }

        REDUCT_MEMCPY(reduct->regs + target, argv, argc * sizeof(reduct_handle_t));

        reduct_uint32_t initialFrameCount = reduct->frameCount;
        reduct_eval_push_frame(reduct, closure, target);

        return reduct_eval_run(reduct, initialFrameCount);
    }

    REDUCT_ERROR_RUNTIME(reduct, "cannot call value");
}