#ifndef REDUCT_COMPILE_H
#define REDUCT_COMPILE_H 1

#include <reduct/bitmap.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/item.h>
#include <reduct/list.h>

/**
 * @file emit.h
 * @brief Bytecode compilation.
 * @defgroup compile Compilation
 *
 * The compilation process converts S-expressions into register-based bytecode that can be executed by the Reduct
 * virtual machine / evaluator.
 *
 * @{
 */

/**
 * @brief Expression descriptor structure.
 * @struct reduct_expr_t
 */
typedef struct reduct_expr
{
    reduct_mode_t mode; ///< Expression mode.
    union {
        uint16_t value;          ///< Raw union value
        reduct_reg_t reg;        ///< Register index
        reduct_const_t constant; ///< Constant index
    };
} reduct_expr_t;

#define REDUCT_JUMP_INVALID ((size_t)-1) ///< Sentinel value for an invalid jump.

/**
 * @brief Local structure.
 * @struct reduct_local_t
 */
typedef struct reduct_local
{
    reduct_atom_t* name; ///< The name of the local variable.
    reduct_expr_t expr;  ///< The expression representing the local's value.
} reduct_local_t;

/**
 * @brief Compiler structure.
 * @struct reduct_compiler_t
 */
typedef struct reduct_compiler
{
    struct reduct_compiler* enclosing;                                 ///< The enclosing compiler context, or `NULL`.
    reduct_t* reduct;                                                  ///< Pointer to the Reduct structure.
    reduct_function_t* function;                                       ///< The function being compiled.
    uint16_t localCount;                                               ///< The amount of local variables.
    reduct_bitmap_t regAlloc[REDUCT_BITMAP_SIZE(REDUCT_REGISTER_MAX)]; ///< Bitmask of allocated registers.
    reduct_bitmap_t regLocal[REDUCT_BITMAP_SIZE(REDUCT_REGISTER_MAX)]; ///< Bitmask of registers used by locals.
    reduct_local_t locals[REDUCT_REGISTER_MAX];                        ///< The local variables.
    reduct_item_t* lastItem; ///< The last item processed by the compiler, used for error reporting.
} reduct_compiler_t;

/**
 * @brief Compiles a Reduct AST into a callable bytecode function.
 *
 * @warning The jump buffer must have been set using `REDUCT_CATCH` before calling this function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param ast The root AST item to compile (usually a list of expressions).
 * @return Handle to the compiled function.
 */
REDUCT_API reduct_handle_t reduct_compile(reduct_t* reduct, reduct_handle_t ast);

/**
 * @brief Initialize a compiler context.
 *
 * @param compiler The compiler context to initialize.
 * @param reduct Pointer to the Reduct structure.
 * @param function The function to compile into.
 * @param enclosing The enclosing compiler context, or `NULL`.
 */
REDUCT_API void reduct_compiler_init(reduct_compiler_t* compiler, reduct_t* reduct, reduct_function_t* function,
    reduct_compiler_t* enclosing);

/**
 * @brief Deinitialize a compiler context.
 *
 * @param compiler The compiler context to deinitialize.
 */
REDUCT_API void reduct_compiler_deinit(reduct_compiler_t* compiler);

/**
 * @brief Set a register as allocated.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to set as allocated.
 */
#define REDUCT_REG_SET_ALLOCATED(_compiler, _reg) \
    do \
    { \
        REDUCT_BITMAP_SET((_compiler)->regAlloc, (_reg)); \
        if ((_reg) + 1 > (_compiler)->function->registerCount) \
        { \
            (_compiler)->function->registerCount = (_reg) + 1; \
        } \
    } while (0)

/**
 * @brief Clear a register's allocation status.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to clear.
 */
#define REDUCT_REG_CLEAR_ALLOCATED(_compiler, _reg) REDUCT_BITMAP_CLEAR((_compiler)->regAlloc, (_reg))

/**
 * @brief Check if a register is allocated.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to check.
 */
#define REDUCT_REG_IS_ALLOCATED(_compiler, _reg) REDUCT_BITMAP_TEST((_compiler)->regAlloc, (_reg))

/**
 * @brief Set a register as a local.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to set as a local.
 */
#define REDUCT_REG_SET_LOCAL(_compiler, _reg) REDUCT_BITMAP_SET((_compiler)->regLocal, (_reg))

/**
 * @brief Clear a register's local status.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to clear.
 */
#define REDUCT_REG_CLEAR_LOCAL(_compiler, _reg) REDUCT_BITMAP_CLEAR((_compiler)->regLocal, (_reg))

/**
 * @brief Check if a register is a local.
 *
 * @param _compiler The compiler instance.
 * @param _reg The register to check.
 */
#define REDUCT_REG_IS_LOCAL(_compiler, _reg) REDUCT_BITMAP_TEST((_compiler)->regLocal, (_reg))

/**
 * @brief Allocate a new register.
 *
 * @param compiler The compiler context.
 * @return The allocated register index.
 */
REDUCT_API reduct_reg_t reduct_reg_alloc(reduct_compiler_t* compiler);

/**
 * @brief Allocate a range of registers.
 *
 * @param compiler The compiler context.
 * @param count The number of registers to allocate.
 * @return The first register index in the allocated range.
 */
REDUCT_API reduct_reg_t reduct_reg_alloc_range(reduct_compiler_t* compiler, uint32_t count);

/**
 * @brief Free a register.
 *
 * @param compiler The compiler context.
 * @param reg The register index to free.
 */
REDUCT_API void reduct_reg_free(reduct_compiler_t* compiler, reduct_reg_t reg);

/**
 * @brief Free a range of registers.
 *
 * @param compiler The compiler context.
 * @param start The first register index in the range to free.
 * @param count The number of registers to free.
 */
REDUCT_API void reduct_reg_free_range(reduct_compiler_t* compiler, reduct_reg_t start, uint32_t count);

/**
 * @brief Create a `REDUCT_OPCODE_MODE_NONE` mode expression.
 */
#define REDUCT_EXPR_NONE() ((reduct_expr_t){.mode = REDUCT_OPCODE_MODE_NONE})

/**
 * @brief Create a `REDUCT_OPCODE_MODE_REG` mode expression.
 *
 * @param _reg The register index.
 */
#define REDUCT_EXPR_REG(_reg) ((reduct_expr_t){.mode = REDUCT_OPCODE_MODE_REG, .reg = (_reg)})

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression.
 *
 * @param _const The constant index.
 */
#define REDUCT_EXPR_CONST(_const) ((reduct_expr_t){.mode = REDUCT_OPCODE_MODE_CONST, .constant = (_const)})

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for a specific item.
 *
 * @param _compiler The compiler context.
 * @param _item The item to look up.
 */
#define REDUCT_EXPR_ITEM(_compiler, _item) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_FROM_ITEM(_item)}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for a specific handle.
 *
 * @param _compiler The compiler context.
 * @param _handle The handle to look up.
 */
#define REDUCT_EXPR_HANDLE(_compiler, _handle) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = (_handle)}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for a specific atom.
 *
 * @param _compiler The compiler context.
 * @param _atom The atom to look up.
 */
#define REDUCT_EXPR_ATOM(_compiler, _atom) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_FROM_ATOM(_atom)}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_TARGET` mode expression.
 *
 * @param _reg The target register hint.
 */
#define REDUCT_EXPR_TARGET(_reg) ((reduct_expr_t){.mode = REDUCT_OPCODE_MODE_TARGET, .reg = (_reg)})

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for the true constant.
 *
 * @param _compiler The compiler context.
 */
#define REDUCT_EXPR_TRUE(_compiler) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_TRUE()}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for the false constant.
 *
 * @param _compiler The compiler context.
 */
#define REDUCT_EXPR_FALSE(_compiler) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_FALSE()}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for the PI constant.
 *
 * @param _compiler The compiler context.
 */
#define REDUCT_EXPR_PI(_compiler) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_PI()}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for the E constant.
 *
 * @param _compiler The compiler context.
 */
#define REDUCT_EXPR_E(_compiler) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, .handle = REDUCT_HANDLE_E()}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for the nil constant.
 *
 * @param _compiler The compiler context.
 */
#define REDUCT_EXPR_NIL(_compiler) \
    REDUCT_EXPR_CONST(reduct_function_lookup_constant((_compiler)->reduct, (_compiler)->function, \
        &(reduct_const_slot_t){.type = REDUCT_CONST_SLOT_TYPE_HANDLE, \
            .handle = REDUCT_HANDLE_NIL((_compiler)->reduct)}))

/**
 * @brief Create a `REDUCT_OPCODE_MODE_CONST` mode expression for a number.
 *
 * @param _compiler The compiler context.
 * @param _val The number value.
 */
#define REDUCT_EXPR_NUMBER(_compiler, _val) \
    REDUCT_EXPR_ATOM(_compiler, reduct_atom_new_number((_compiler)->reduct, (_val)))

/**
 * @brief Get the target register index from an expression, or -1 if no target is specified.
 * @param _expr The expression to check.
 */
#define REDUCT_EXPR_GET_TARGET(_expr) (((_expr)->mode == REDUCT_OPCODE_MODE_TARGET) ? (_expr)->reg : REDUCT_REG_INVALID)

/**
 * @brief Compiles a single Reduct handle into an expression descriptor.
 *
 * @param compiler The compiler context.
 * @param handle The handle to compile.
 * @param out Output pointer for the compiled expression.
 */
REDUCT_API void reduct_expr_build(reduct_compiler_t* compiler, reduct_handle_t handle, reduct_expr_t* out);

/**
 * @brief Free resources associated with an expression descriptor.
 *
 * @param compiler The compiler context.
 * @param expr The expression to free.
 */
static inline void reduct_expr_done(reduct_compiler_t* compiler, reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    if (expr->mode == REDUCT_OPCODE_MODE_REG)
    {
        reduct_reg_free(compiler, expr->reg);
    }
}

/**
 * @brief Allocate a new register, favoring the output expression's target if provided.
 *
 * @param compiler The compiler context.
 * @param out The output expression which may contain a target hint.
 * @return The allocated register index.
 */
static inline reduct_reg_t reduct_expr_get_reg(reduct_compiler_t* compiler, reduct_expr_t* out)
{
    assert(compiler != NULL);
    if (out != NULL && out->mode == REDUCT_OPCODE_MODE_TARGET)
    {
        return out->reg;
    }
    return reduct_reg_alloc(compiler);
}

/**
 * @brief Get the first unallocated register index.
 *
 * @param compiler The compiler context.
 * @return The first register index that is not currently allocated.
 */
static inline reduct_reg_t reduct_reg_get_base(reduct_compiler_t* compiler)
{
    for (reduct_reg_t i = REDUCT_REGISTER_MAX; i-- > 0;)
    {
        if (REDUCT_REG_IS_ALLOCATED(compiler, i))
        {
            return (reduct_reg_t)(i + 1);
        }
    }
    return 0;
}

/**
 * @brief Check if a local variable has finished being defined.
 *
 * If the local variable is not defined, then we are currently within the expression that defines it.
 *
 * @param _local The local variable descriptor.
 */
#define REDUCT_LOCAL_IS_DEFINED(_local) ((_local)->expr.mode != REDUCT_OPCODE_MODE_NONE)

/**
 * @brief Define a new local variable.
 *
 * @note The `reduct_local_def_done()` function must be called after this one.
 *
 * @param compiler The compiler context.
 * @param name The name of the local.
 * @return A pointer to the local variable descriptor.
 */
REDUCT_API reduct_local_t* reduct_local_def(reduct_compiler_t* compiler, reduct_atom_t* name);

/**
 * @brief Finalize a local variable definition with its value expression.
 *
 * @param compiler The compiler context.
 * @param local The local variable descriptor.
 * @param expr The expression representing the local's value.
 */
REDUCT_API void reduct_local_def_done(reduct_compiler_t* compiler, reduct_local_t* local, reduct_expr_t* expr);

/**
 * @brief Add a function argument local to the compiler context.
 *
 * @param compiler The compiler context.
 * @param name The name of the argument.
 * @return A pointer to the local variable descriptor.
 */
REDUCT_API reduct_local_t* reduct_local_add_arg(reduct_compiler_t* compiler, reduct_atom_t* name);

/**
 * @brief Pop local variables from the stack, releasing their registers if they are no longer used.
 *
 * @param compiler The compiler context.
 * @param toCount The local count to restore to.
 * @param result The result expression of the block, whose register should not be freed.
 */
REDUCT_API void reduct_local_pop(reduct_compiler_t* compiler, uint16_t toCount, reduct_expr_t* result);

/**
 * @brief Look up a local by name and return its expression.
 *
 * @param compiler The compiler context.
 * @param name The name of the local.
 * @return A pointer to the local variable descriptor, or `NULL` if not found.
 */
REDUCT_API reduct_local_t* reduct_local_lookup(reduct_compiler_t* compiler, reduct_atom_t* name);

/**
 * @brief Emits an instruction to the current function.
 *
 * @param compiler The compiler context.
 * @param inst The instruction to emit.
 */
static inline void reduct_compile_inst(reduct_compiler_t* compiler, reduct_inst_t inst)
{
    assert(compiler != NULL);
    uint32_t pos = compiler->lastItem != NULL ? compiler->lastItem->position : 0;
    reduct_function_emit(compiler->reduct, compiler->function, inst, pos);
}

/**
 * @brief Emits a `REDUCT_OPCODE_LIST` instruction, that creates a list in the target register.
 *
 * @param compiler The compiler context.
 * @param target The target register.
 * @param count The number of elements to include in the list.
 */
static inline void reduct_compile_list(reduct_compiler_t* compiler, reduct_reg_t target, uint32_t count)
{
    assert(compiler != NULL);
    reduct_compile_inst(compiler, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_LIST, target, count, 0));
}

/**
 * @brief Emits a `REDUCT_OPCODE_CALL` instruction, that returns its result in the target register.
 *
 * @param compiler The compiler context.
 * @param target The target register.
 * @param callable The callable expression.
 * @param arity The number of arguments.
 */
static inline void reduct_compile_call(reduct_compiler_t* compiler, reduct_reg_t target, reduct_expr_t* callable,
    uint32_t arity)
{
    assert(compiler != NULL);
    assert(callable != NULL);
    assert(callable->mode == REDUCT_OPCODE_MODE_REG || callable->mode == REDUCT_OPCODE_MODE_CONST);
    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_CALL | callable->mode), target, arity, callable->value));
}

/**
 * @brief Emits a `REDUCT_OPCODE_RECUR` instruction, that returns its result in the target register.
 *
 * @param compiler The compiler context.
 * @param target The target register (also base of arguments).
 * @param arity The number of arguments.
 */
static inline void reduct_compile_recur(reduct_compiler_t* compiler, reduct_reg_t target, uint32_t arity)
{
    assert(compiler != NULL);
    reduct_compile_inst(compiler, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_RECUR, target, arity, 0));
}

/**
 * @brief Emits a `REDUCT_OPCODE_MOVE` instruction, that moves the value of the source expression to the target
 * register.
 *
 * @param compiler The compiler context.
 * @param target The target register.
 * @param expr The source expression.
 */
static inline void reduct_compile_move(reduct_compiler_t* compiler, reduct_reg_t target, reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    assert(expr->mode == REDUCT_OPCODE_MODE_REG || expr->mode == REDUCT_OPCODE_MODE_CONST);
    assert(target < REDUCT_REGISTER_MAX);
    assert(expr->mode != REDUCT_OPCODE_MODE_REG || expr->reg < REDUCT_REGISTER_MAX);

    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_MOV | (reduct_opcode_t)expr->mode), target, 0,
            expr->value));
}

/**
 * @brief Emits a jump instruction without a target offset.
 *
 * @param compiler The compiler context.
 * @param op The jump opcode (e.g., `REDUCT_OPCODE_JMP`, `REDUCT_OPCODE_JMPT`, `REDUCT_OPCODE_JMPF`).
 * @param a The register to test (if not `REDUCT_OPCODE_JMP`).
 * @return The index of the emitted instruction to be patched later.
 */
static inline size_t reduct_compile_jump(reduct_compiler_t* compiler, reduct_opcode_t op, reduct_reg_t a)
{
    assert(compiler != NULL);
    size_t pos = compiler->function->instCount;
    reduct_compile_inst(compiler, REDUCT_INST_MAKE_ASBX(op, a, 0));
    return pos;
}

/**
 * @brief Patch a previously emitted jump instruction to point to the current instruction.
 *
 * @param compiler The compiler context.
 * @param pos The index of the jump instruction to patch.
 */
static inline void reduct_compile_jump_patch(reduct_compiler_t* compiler, size_t pos)
{
    assert(compiler != NULL);
    int64_t offset = (int64_t)(compiler->function->instCount - pos - 1);
    compiler->function->insts[pos] = REDUCT_INST_SET_SBX(compiler->function->insts[pos], offset);
}

/**
 * @brief Patch a list of jump instructions to point to the current instruction.
 *
 * @param compiler The compiler context.
 * @param jumps Array of jump instruction indices.
 * @param count Number of jumps in the array.
 */
static inline void reduct_compile_jump_patch_list(reduct_compiler_t* compiler, size_t* jumps, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        reduct_compile_jump_patch(compiler, jumps[i]);
    }
}

/**
 * @brief Emits a move instruction or allocates a new register if the expression is not already in a register.
 *
 * @param compiler The compiler context.
 * @param expr The expression to move or allocate.
 * @return The register where the value is stored.
 */
static inline reduct_reg_t reduct_compile_move_or_alloc(reduct_compiler_t* compiler, reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    assert(expr->mode == REDUCT_OPCODE_MODE_REG || expr->mode == REDUCT_OPCODE_MODE_CONST);

    if (expr->mode == REDUCT_OPCODE_MODE_REG)
    {
        return expr->reg;
    }

    reduct_reg_t target = reduct_reg_alloc(compiler);
    reduct_compile_move(compiler, target, expr);
    *expr = REDUCT_EXPR_REG(target);
    return target;
}

/**
 * @brief Emits a `REDUCT_OPCODE_RET` instruction.
 *
 * @param compiler The compiler context.
 * @param expr The expression to return.
 */
REDUCT_API void reduct_compile_return(reduct_compiler_t* compiler, reduct_expr_t* expr);

/**
 * @brief Emits a comparison, arithmetic or bitwise instruction.
 *
 * @param compiler The compiler context.
 * @param opBase The base opcode (without a mode) for the operation (e.g, `REDUCT_OPCODE_ADD`, `REDUCT_OPCODE_EQ`).
 * @param target Pointer to the target register, can be `REDUCT_REG_INVALID`, might be updated.
 * @param left The left operand register.
 * @param right The right operand expression.
 */
static inline void reduct_compile_binary(reduct_compiler_t* compiler, reduct_opcode_t opBase, reduct_reg_t* target,
    reduct_reg_t left, reduct_expr_t* right)
{
    assert(compiler != NULL);
    assert(right != NULL);
    assert(right->mode == REDUCT_OPCODE_MODE_REG || right->mode == REDUCT_OPCODE_MODE_CONST);

    if (*target == REDUCT_REG_INVALID)
    {
        *target = reduct_reg_alloc(compiler);
    }

    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(opBase | right->mode), *target, left, right->value));
}

/**
 * @brief Emits a skip-comparison instruction (JEQ, JNEQ, JLT, JLE, JGT, JGE) that skips the next
 * instruction if the comparison is true.
 *
 * @param compiler The compiler context.
 * @param opBase The opcode (e.g, `REDUCT_OPCODE_JEQ`).
 * @param left The left operand register.
 * @param right The right operand expression.
 */
static inline void reduct_compile_skip(reduct_compiler_t* compiler, reduct_opcode_t opBase, reduct_reg_t left,
    reduct_expr_t* right)
{
    assert(compiler != NULL);
    assert(right != NULL);
    assert(right->mode == REDUCT_OPCODE_MODE_REG || right->mode == REDUCT_OPCODE_MODE_CONST);
    reduct_compile_inst(compiler, REDUCT_INST_MAKE_ABC((reduct_opcode_t)(opBase | right->mode), left, 0, right->value));
}

/**
 * @brief Emits a `REDUCT_OPCODE_CLOSURE` instruction.
 *
 * @param compiler The compiler context.
 * @param target The target register.
 * @param funcConst The constant index of the function prototype.
 */
static inline void reduct_compile_closure(reduct_compiler_t* compiler, reduct_reg_t target, reduct_const_t funcConst)
{
    assert(compiler != NULL);
    reduct_compile_inst(compiler, REDUCT_INST_MAKE_ABC(REDUCT_OPCODE_CLOSURE, target, 0, funcConst));
}

/**
 * @brief Emits a `REDUCT_OPCODE_CAPTURE` instruction.
 *
 * @param compiler The compiler context.
 * @param closureReg The register containing the closure.
 * @param slot The constant slot index in the closure to capture into.
 * @param expr The expression to be captured.
 */
static inline void reduct_compile_capture(reduct_compiler_t* compiler, reduct_reg_t closureReg, uint32_t slot,
    reduct_expr_t* expr)
{
    assert(compiler != NULL);
    assert(expr != NULL);
    assert(expr->mode == REDUCT_OPCODE_MODE_REG || expr->mode == REDUCT_OPCODE_MODE_CONST);
    reduct_compile_inst(compiler,
        REDUCT_INST_MAKE_ABC((reduct_opcode_t)(REDUCT_OPCODE_CAPTURE | (reduct_opcode_t)expr->mode), closureReg, slot,
            expr->value));
}

/** @} */

#endif
