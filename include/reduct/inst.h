#ifndef REDUCT_INST_H
#define REDUCT_INST_H 1

#include "reduct/defs.h"

/**
 * @file inst.h
 * @brief Bytecode instruction format.
 * @defgroup inst Instruction Format
 *
 * Instructions are 32-bit words
 * with the following formats:
 *
 * - iABC:  [ Opcode:6 | A:8 | B:8 | C:10 ]
 * - iAsBx: [ Opcode:6 | A:8 | sBx:18 ]
 *
 * Fields:
 * - A: Usually the target/destination register.
 * - B: Usually the first operand (register).
 * - C: Usually the second operand (register or constant).
 * - sBx: Signed offsets for jumps.
 *
 * To determine if the C field is a register or a constant the `reduct_mode_t` flags are used to modify the opcode.
 *
 * @note The reason we avoid formats such as iABx, used within Lua, is that even if it increases the maximum constant
 * capacity it means that operations such as `REDUCT_OPCODE_EQUAL` always need to act on registers, which introduces
 * unnecessary `MOV` instructions to load constants into registers before they can be compared.
 *
 * @{
 */

/**
 * @brief Opcode mode enumeration.
 * @enum reduct_mode_t
 */
typedef enum
{
    REDUCT_MODE_NONE = -1,      ///< Invalid mode.
    REDUCT_MODE_TARGET = -2,    ///< Compilation target hint mode.
    REDUCT_MODE_REG = 0,        ///< Register operand mode.
    REDUCT_MODE_CONST = 1 << 5, ///< Constant operand mode.
} reduct_mode_t;

/**
 * @brief Opcode enumeration.
 * @enum reduct_opcode_t
 */
typedef enum
{
    REDUCT_OPCODE_NONE,
    REDUCT_OPCODE_LIST,     ///< (A, B) R(A) = (R(A) R(A + 1) ... R(A + B - 1))
    REDUCT_OPCODE_JMP,      ///< (sBx) Unconditional jump by relative offset sBx.
    REDUCT_OPCODE_JMPF,     ///< (A, sBx) Jump by sBx if R(A) is falsy.
    REDUCT_OPCODE_JMPT,     ///< (A, sBx) Jump by sBx if R(A) is truthy.
    REDUCT_OPCODE_JEQ,      ///< (A, C) Skip the next instruction if R(A) == R/K(C), else continue.
    REDUCT_OPCODE_CALL,     ///< (A, B, C) Call callable in R/K(C) with B args starting from R(A). Result in R(A).
    REDUCT_OPCODE_MOV,      ///< (A, C) Move value in R/K(C) to R(A).
    REDUCT_OPCODE_RET,      ///< (C) Return value in R/K(C).
    REDUCT_OPCODE_EQ,       ///< (A, B, C) If R(B) == R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_NEQ,      ///< (A, B, C) If R(B) != R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_SEQ,      ///< (A, B, C) If R(B) === R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_SNEQ,     ///< (A, B, C) If R(B) !== R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LT,       ///< (A, B, C) If R(B) < R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LE,       ///< (A, B, C) If R(B) <= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GT,       ///< (A, B, C) If R(B) > R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GE,       ///< (A, B, C) If R(B) >= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_ADD,      ///< (A, B, C) R(A) = R(B) + R/K(C)
    REDUCT_OPCODE_SUB,      ///< (A, B, C) R(A) = R(B) - R/K(C)
    REDUCT_OPCODE_MUL,      ///< (A, B, C) R(A) = R(B) * R/K(C)
    REDUCT_OPCODE_DIV,      ///< (A, B, C) R(A) = R(B) / R/K(C)
    REDUCT_OPCODE_MOD,      ///< (A, B, C) R(A) = R(B) % R/K(C)
    REDUCT_OPCODE_BAND,     ///< (A, B, C) R(A) = R(B) & R/K(C)
    REDUCT_OPCODE_BOR,      ///< (A, B, C) R(A) = R(B) | R/K(C)
    REDUCT_OPCODE_BXOR,     ///< (A, B, C) R(A) = R(B) ^ R/K(C)
    REDUCT_OPCODE_BNOT,     ///< (A, C) R(A) = ~R/K(C)
    REDUCT_OPCODE_SHL,      ///< (A, B, C) R(A) = R(B) << R/K(C)
    REDUCT_OPCODE_SHR,      ///< (A, B, C) R(A) = R(B) >> R/K(C)
    REDUCT_OPCODE_CLOSURE,  ///< (A, C) Wrap the function prototype in K(C) in a closure and store in R(A).
    REDUCT_OPCODE_CAPTURE,  ///< (A, B, C) Capture R/K(C) into constant slot B in closure R(A).
    REDUCT_OPCODE_TAILCALL, ///< (A, B, C) Tail call callable in R/K(C) with B args starting from R(A).
} reduct_opcode_t;

/**
 * @brief Register type.
 */
typedef reduct_uint16_t reduct_reg_t;

/**
 * @brief Invalid register value.
 */
#define REDUCT_REG_INVALID ((reduct_reg_t) - 1)

/**
 * @brief Instruction type.
 */
typedef reduct_uint32_t reduct_inst_t;

#define REDUCT_INST_WIDTH_OPCODE 6ULL                                     ///< Opcode width in bits.
#define REDUCT_INST_WIDTH_A 8ULL                                          ///< A operand width in bits.
#define REDUCT_INST_WIDTH_B 8ULL                                          ///< B operand width in bits.
#define REDUCT_INST_WIDTH_C 10ULL                                         ///< C operand width in bits.
#define REDUCT_INST_WIDTH_SBX (REDUCT_INST_WIDTH_B + REDUCT_INST_WIDTH_C) ///< SBx operand width in bits.

/**
 * @brief The max number of registers per function frame.
 */
#define REDUCT_REGISTER_MAX (1ULL << REDUCT_INST_WIDTH_A)
/**
 * @brief The max number of constants per function.
 */
#define REDUCT_CONSTANT_MAX (1ULL << REDUCT_INST_WIDTH_C)

#define REDUCT_INST_POS_OPCODE 0ULL                                           ///< Opcode position in bits.
#define REDUCT_INST_POS_A (REDUCT_INST_POS_OPCODE + REDUCT_INST_WIDTH_OPCODE) ///< A operand position in bits.
#define REDUCT_INST_POS_B (REDUCT_INST_POS_A + REDUCT_INST_WIDTH_A)           ///< B operand position in bits.
#define REDUCT_INST_POS_C (REDUCT_INST_POS_B + REDUCT_INST_WIDTH_B)           ///< C operand position in bits.

#define REDUCT_INST_MASK_OPCODE ((1ULL << REDUCT_INST_WIDTH_OPCODE) - 1ULL) ///< Opcode mask.
#define REDUCT_INST_MASK_A ((1ULL << REDUCT_INST_WIDTH_A) - 1ULL)           ///< A operand mask.
#define REDUCT_INST_MASK_B ((1ULL << REDUCT_INST_WIDTH_B) - 1ULL)           ///< B operand mask.
#define REDUCT_INST_MASK_C ((1ULL << REDUCT_INST_WIDTH_C) - 1ULL)           ///< C operand mask.
#define REDUCT_INST_MASK_SBX ((1ULL << REDUCT_INST_WIDTH_SBX) - 1ULL)       ///< SBx operand mask.

/**
 * @brief Create an instruction with opcode, A, B, and C operands.
 *
 * @param _op Opcode operand.
 * @param _a A operand.
 * @param _b B operand.
 * @param _c C operand.
 */
#define REDUCT_INST_MAKE_ABC(_op, _a, _b, _c) \
    ((((reduct_inst_t)(_op)) & REDUCT_INST_MASK_OPCODE) << REDUCT_INST_POS_OPCODE | \
        (((reduct_inst_t)(_a)) & REDUCT_INST_MASK_A) << REDUCT_INST_POS_A | \
        (((reduct_inst_t)(_b)) & REDUCT_INST_MASK_B) << REDUCT_INST_POS_B | \
        (((reduct_inst_t)(_c)) & REDUCT_INST_MASK_C) << REDUCT_INST_POS_C)

/**
 * @brief Create an instruction with opcode and A operands, and SBx B operand.
 *
 * @param _op Opcode operand.
 * @param _a A operand.
 * @param _sbx SBx operand.
 */
#define REDUCT_INST_MAKE_ASBX(_op, _a, _sbx) \
    ((((reduct_inst_t)(_op)) & REDUCT_INST_MASK_OPCODE) << REDUCT_INST_POS_OPCODE | \
        (((reduct_inst_t)(_a)) & REDUCT_INST_MASK_A) << REDUCT_INST_POS_A | \
        (((reduct_inst_t)(_sbx)) & REDUCT_INST_MASK_SBX) << REDUCT_INST_POS_B)

/**
 * @brief Get the opcode from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_OP(_inst) (((_inst) >> REDUCT_INST_POS_OPCODE) & REDUCT_INST_MASK_OPCODE)

/**
 * @brief Get the opcode base (without `reduct_mode_t`) from an instruction. Mask clears the `REDUCT_MODE_CONST` bit.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_OP_BASE(_inst) \
    (((_inst) >> REDUCT_INST_POS_OPCODE) & (REDUCT_INST_MASK_OPCODE & ~REDUCT_MODE_CONST))

/**
 * @brief Get the A operand from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_A(_inst) (((_inst) >> REDUCT_INST_POS_A) & REDUCT_INST_MASK_A)

/**
 * @brief Get the B operand from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_B(_inst) (((_inst) >> REDUCT_INST_POS_B) & REDUCT_INST_MASK_B)

/**
 * @brief Get the C operand from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_C(_inst) (((_inst) >> REDUCT_INST_POS_C) & REDUCT_INST_MASK_C)

/**
 * @brief Get the SBX operand from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_SBX(_inst) \
    ((reduct_int64_t)(((_inst) >> REDUCT_INST_POS_B) & REDUCT_INST_MASK_SBX) << (32ULL - REDUCT_INST_WIDTH_SBX) >> \
        (32ULL - REDUCT_INST_WIDTH_SBX))

/**
 * @brief Set the A operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _a A operand value.
 */
#define REDUCT_INST_SET_A(_inst, _a) \
    (((_inst) & ~(REDUCT_INST_MASK_A << REDUCT_INST_POS_A)) | (((_a) & REDUCT_INST_MASK_A) << REDUCT_INST_POS_A))

/**
 * @brief Set the SBX operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _sbx SBX operand value.
 */
#define REDUCT_INST_SET_SBX(_inst, _sbx) \
    (((_inst) & ~(REDUCT_INST_MASK_SBX << REDUCT_INST_POS_B)) | (((_sbx) & REDUCT_INST_MASK_SBX) << REDUCT_INST_POS_B))

#endif
