#ifndef REDUCT_INST_H
#define REDUCT_INST_H 1

#include "reduct/defs.h"

/**
 * @file inst.h
 * @brief Bytecode instruction format.
 * @defgroup inst Instruction Format
 *
 * Instructions are 32-bit words with the following formats:
 *
 * - iABC:  [ Opcode:8 | A:8 | B:8 | C:8 ]
 * - iAsBx: [ Opcode:8 | A:8 | sBx:16 ]
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
    REDUCT_OPCODE_LIST = 0b000000,                               ///< (A, B) R(A) = (R(A) R(A + 1) ... R(A + B - 1))
    REDUCT_OPCODE_JMP = REDUCT_OPCODE_LIST | REDUCT_MODE_CONST,  ///< (sBx) Unconditional jump by relative offset sBx.
    REDUCT_OPCODE_JMPF = 0b000001,                               ///< (A, sBx) Jump by sBx if R(A) is falsy.
    REDUCT_OPCODE_JMPT = REDUCT_OPCODE_JMPF | REDUCT_MODE_CONST, ///< (A, sBx) Jump by sBx if R(A) is truthy.
    REDUCT_OPCODE_CALL =
        0b000010, ///< (A, B, C) Call callable in R/K(C) with B args starting from R(A). Result in R(A).
    REDUCT_OPCODE_CALL_CONST = REDUCT_OPCODE_CALL | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_CALL`.
    REDUCT_OPCODE_MOV = 0b000011,                                      ///< (A, C) Move value in R/K(C) to R(A).
    REDUCT_OPCODE_MOV_CONST = REDUCT_OPCODE_MOV | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_MOV`.
    REDUCT_OPCODE_RET = 0b000100,                                      ///< (C) Return value in R/K(C).
    REDUCT_OPCODE_RET_CONST = REDUCT_OPCODE_RET | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_RET`.
    REDUCT_OPCODE_EQ = 0b000101, ///< (A, B, C) If R(B) == R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_EQ_CONST = REDUCT_OPCODE_EQ | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_EQ`.
    REDUCT_OPCODE_NEQ = 0b000110, ///< (A, B, C) If R(B) != R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_NEQ_CONST = REDUCT_OPCODE_NEQ | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_NEQ`.
    REDUCT_OPCODE_LT = 0b000111, ///< (A, B, C) If R(B) < R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LT_CONST = REDUCT_OPCODE_LT | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_LT`.
    REDUCT_OPCODE_LE = 0b001000, ///< (A, B, C) If R(B) <= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LE_CONST = REDUCT_OPCODE_LE | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_LE`.
    REDUCT_OPCODE_GT = 0b001001, ///< (A, B, C) If R(B) > R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GT_CONST = REDUCT_OPCODE_GT | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_GT`.
    REDUCT_OPCODE_GE = 0b001010, ///< (A, B, C) If R(B) >= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GE_CONST = REDUCT_OPCODE_GE | REDUCT_MODE_CONST,     ///< Constant version of `REDUCT_OPCODE_GE`.
    REDUCT_OPCODE_ADD = 0b001011,                                      ///< (A, B, C) R(A) = R(B) + R/K(C)
    REDUCT_OPCODE_ADD_CONST = REDUCT_OPCODE_ADD | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_ADD`.
    REDUCT_OPCODE_SUB = 0b001100,                                      ///< (A, B, C) R(A) = R(B) - R/K(C)
    REDUCT_OPCODE_SUB_CONST = REDUCT_OPCODE_SUB | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_SUB`.
    REDUCT_OPCODE_MUL = 0b001101,                                      ///< (A, B, C) R(A) = R(B) * R/K(C)
    REDUCT_OPCODE_MUL_CONST = REDUCT_OPCODE_MUL | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_MUL`.
    REDUCT_OPCODE_DIV = 0b001110,                                      ///< (A, B, C) R(A) = R(B) / R/K(C)
    REDUCT_OPCODE_DIV_CONST = REDUCT_OPCODE_DIV | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_DIV`.
    REDUCT_OPCODE_MOD = 0b001111,                                      ///< (A, B, C) R(A) = R(B) % R/K(C)
    REDUCT_OPCODE_MOD_CONST = REDUCT_OPCODE_MOD | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_MOD`.
    REDUCT_OPCODE_BAND = 0b010000,                                     ///< (A, B, C) R(A) = R(B) & R/K(C)
    REDUCT_OPCODE_BAND_CONST = REDUCT_OPCODE_BAND | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_BAND`.
    REDUCT_OPCODE_BOR = 0b010001,                                      ///< (A, B, C) R(A) = R(B) | R/K(C)
    REDUCT_OPCODE_BOR_CONST = REDUCT_OPCODE_BOR | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_BOR`.
    REDUCT_OPCODE_BXOR = 0b010010,                                     ///< (A, B, C) R(A) = R(B) ^ R/K(C)
    REDUCT_OPCODE_BXOR_CONST = REDUCT_OPCODE_BXOR | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_BXOR`.
    REDUCT_OPCODE_BNOT = 0b010011,                                     ///< (A, C) R(A) = ~R/K(C)
    REDUCT_OPCODE_BNOT_CONST = REDUCT_OPCODE_BNOT | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_BNOT`.
    REDUCT_OPCODE_SHL = 0b010100,                                      ///< (A, B, C) R(A) = R(B) << R/K(C)
    REDUCT_OPCODE_SHL_CONST = REDUCT_OPCODE_SHL | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_SHL`.
    REDUCT_OPCODE_SHR = 0b010101,                                      ///< (A, B, C) R(A) = R(B) >> R/K(C)
    REDUCT_OPCODE_SHR_CONST = REDUCT_OPCODE_SHR | REDUCT_MODE_CONST,   ///< Constant version of `REDUCT_OPCODE_SHR`.
    REDUCT_OPCODE_CLOSURE = 0b010110, ///< (A, C) Wrap the function prototype in K(C) in a closure and store in R(A).
    REDUCT_OPCODE_NOP = REDUCT_OPCODE_CLOSURE | REDUCT_MODE_CONST, ///< No operation.
    REDUCT_OPCODE_CAPTURE = 0b010111, ///< (A, B, C) Capture R/K(C) into constant slot B in closure R(A).
    REDUCT_OPCODE_CAPTURE_CONST =
        REDUCT_OPCODE_CAPTURE | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_CAPTURE`.
    REDUCT_OPCODE_TAILCALL = 0b011000, ///< (A, B, C) Tail call callable in R/K(C) with B args starting from R(A).
    REDUCT_OPCODE_TAILCALL_CONST =
        REDUCT_OPCODE_TAILCALL | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_TAILCALL`.
    REDUCT_OPCODE_JEQ = 0b011001, ///< (A, C) Skip the next instruction if R(A) == R/K(C), else continue.
    REDUCT_OPCODE_JEQ_CONST = REDUCT_OPCODE_JEQ | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JEQ`.
    REDUCT_OPCODE_JNEQ = 0b011010, ///< (A, C) Skip the next instruction if R(A) != R/K(C), else continue.
    REDUCT_OPCODE_JNEQ_CONST = REDUCT_OPCODE_JNEQ | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JNEQ`.
    REDUCT_OPCODE_JLT = 0b011011, ///< (A, C) Skip the next instruction if R(A) < R/K(C), else continue.
    REDUCT_OPCODE_JLT_CONST = REDUCT_OPCODE_JLT | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JLT`.
    REDUCT_OPCODE_JLE = 0b011100, ///< (A, C) Skip the next instruction if R(A) <= R/K(C), else continue.
    REDUCT_OPCODE_JLE_CONST = REDUCT_OPCODE_JLE | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JLE`.
    REDUCT_OPCODE_JGT = 0b011101, ///< (A, C) Skip the next instruction if R(A) > R/K(C), else continue.
    REDUCT_OPCODE_JGT_CONST = REDUCT_OPCODE_JGT | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JGT`.
    REDUCT_OPCODE_JGE = 0b011110, ///< (A, C) Skip the next instruction if R(A) >= R/K(C), else continue.
    REDUCT_OPCODE_JGE_CONST = REDUCT_OPCODE_JGE | REDUCT_MODE_CONST, ///< Constant version of `REDUCT_OPCODE_JGE`.
    REDUCT_OPCODE_RECUR =
        0b011111, ///< (A, B) Recursively call the current function with B args starting from R(A). Result in R(A).
    REDUCT_OPCODE_TAILRECUR =
        REDUCT_OPCODE_RECUR | REDUCT_MODE_CONST, ///< (A, B) Recursively tail call the current function with B args
                                                 ///< starting from R(A). Result in R(A).
} reduct_opcode_t;

#define REDUCT_OP_FLAG_HAS_TARGET (1 << 0)     ///< Opcode modifies target register A.
#define REDUCT_OP_FLAG_IS_JUMP (1 << 1)        ///< Opcode is a jump.
#define REDUCT_OP_FLAG_HAS_CONST (1 << 2)      ///< Opcode uses C operand and has both reg/const versions.
#define REDUCT_OP_FLAG_READ_A (1 << 3)         ///< Opcode reads from register A (or range starting at A).
#define REDUCT_OP_FLAG_READ_B (1 << 4)         ///< Opcode reads from register B.
#define REDUCT_OP_FLAG_READ_C (1 << 5)         ///< Opcode reads from register/constant C.
#define REDUCT_OP_FLAG_READ_RANGE (1 << 6)     ///< Opcode reads a range of registers starting at A.
#define REDUCT_OP_FLAG_IS_PURE (1 << 7)        ///< Opcode is pure (no side effects).
#define REDUCT_OP_FLAG_IS_COMMUTATIVE (1 << 8) ///< Opcode is commutative.
#define REDUCT_OP_FLAG_IS_SKIP (1 << 9)        ///< Opcode is a skip instruction.
#define REDUCT_OP_FLAG_IS_CALL (1 << 10)       ///< Opcode is a function call.
#define REDUCT_OP_FLAG_IS_TERMINATOR (1 << 11) ///< Opcode ends basic block reachability.

/**
 * @brief Opcode flags lookup table.
 */
REDUCT_API extern const uint16_t reductOpcodeFlags[UINT8_MAX + 1];

/**
 * @brief Check if an opcode modifies its target register (A).
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_HAS_TARGET(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_HAS_TARGET)

/**
 * @brief Check if an opcode is a jump instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_JUMP(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_JUMP)

/**
 * @brief Check if an opcode uses the C operand and has both a constant and register version.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_HAS_CONST(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_HAS_CONST)

/**
 * @brief Check if an opcode reads from register A.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_A(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_READ_A)

/**
 * @brief Check if an opcode reads from register B.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_B(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_READ_B)

/**
 * @brief Check if an opcode reads from register/constant C.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_C(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_READ_C)

/**
 * @brief Check if an opcode reads a range of registers starting at A.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_RANGE(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_READ_RANGE)

/**
 * @brief Check if an opcode is pure.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_PURE(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_PURE)

/**
 * @brief Check if an opcode is commutative.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_COMMUTATIVE(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_COMMUTATIVE)

/**
 * @brief Check if an opcode is a skip instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_SKIP(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_SKIP)

/**
 * @brief Check if an opcode is a function call.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_CALL(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_CALL)

/**
 * @brief Check if an opcode is a terminator.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_TERMINATOR(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OP_FLAG_IS_TERMINATOR)

/**
 * @brief Check if an instruction reads from a specific register.
 *
 * @param _inst The instruction.
 * @param _reg The register index.
 */
#define REDUCT_INST_READS_REG(_inst, _reg) \
    (REDUCT_OPCODE_READS_A(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_A(REDUCT_INST_GET_OP(_inst)) || \
        (REDUCT_OPCODE_READS_B(REDUCT_INST_GET_OP(_inst)) && \
            (_reg) == REDUCT_INST_GET_B(REDUCT_INST_GET_OP(_inst))) || \
        (REDUCT_OPCODE_READS_C(REDUCT_INST_GET_OP(_inst)) && \
            (_reg) == REDUCT_INST_GET_C(REDUCT_INST_GET_OP(_inst))) || \
        (REDUCT_OPCODE_READS_RANGE(REDUCT_INST_GET_OP(_inst)) && \
            (_reg) >= REDUCT_INST_GET_A(REDUCT_INST_GET_OP(_inst)) && \
            (_reg) < REDUCT_INST_GET_A(REDUCT_INST_GET_OP(_inst)) + REDUCT_INST_GET_B(REDUCT_INST_GET_OP(_inst))))

/**
 * @brief Check if an instruction writes to a specific register.
 *
 * @param _inst The instruction.
 * @param _reg The register index.
 */
#define REDUCT_INST_WRITES_REG(_inst, _reg) \
    (REDUCT_OPCODE_HAS_TARGET(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_A(_inst))

/**
 * @brief Register type.
 */
typedef uint16_t reduct_reg_t;

/**
 * @brief Invalid register value.
 */
#define REDUCT_REG_INVALID ((reduct_reg_t) - 1)

/**
 * @brief Instruction type.
 */
typedef uint32_t reduct_inst_t;

#define REDUCT_INST_WIDTH_OPCODE 8U                                       ///< Opcode width in bits.
#define REDUCT_INST_WIDTH_A 8U                                            ///< A operand width in bits.
#define REDUCT_INST_WIDTH_B 8U                                            ///< B operand width in bits.
#define REDUCT_INST_WIDTH_C 8U                                            ///< C operand width in bits.
#define REDUCT_INST_WIDTH_SBX (REDUCT_INST_WIDTH_B + REDUCT_INST_WIDTH_C) ///< SBx operand width in bits.

/**
 * @brief The max number of registers per function frame.
 */
#define REDUCT_REGISTER_MAX (1U << REDUCT_INST_WIDTH_A)
/**
 * @brief The max number of constants per function.
 */
#define REDUCT_CONSTANT_MAX (1U << REDUCT_INST_WIDTH_C)

#define REDUCT_INST_POS_OPCODE 0U                                             ///< Opcode position in bits.
#define REDUCT_INST_POS_A (REDUCT_INST_POS_OPCODE + REDUCT_INST_WIDTH_OPCODE) ///< A operand position in bits.
#define REDUCT_INST_POS_B (REDUCT_INST_POS_A + REDUCT_INST_WIDTH_A)           ///< B operand position in bits.
#define REDUCT_INST_POS_C (REDUCT_INST_POS_B + REDUCT_INST_WIDTH_B)           ///< C operand position in bits.

#define REDUCT_INST_MASK_OPCODE ((1U << REDUCT_INST_WIDTH_OPCODE) - 1U) ///< Opcode mask.
#define REDUCT_INST_MASK_A ((1U << REDUCT_INST_WIDTH_A) - 1U)           ///< A operand mask.
#define REDUCT_INST_MASK_B ((1U << REDUCT_INST_WIDTH_B) - 1U)           ///< B operand mask.
#define REDUCT_INST_MASK_C ((1U << REDUCT_INST_WIDTH_C) - 1U)           ///< C operand mask.
#define REDUCT_INST_MASK_SBX ((1U << REDUCT_INST_WIDTH_SBX) - 1U)       ///< SBx operand mask.

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
    ((int64_t)(((_inst) >> REDUCT_INST_POS_B) & REDUCT_INST_MASK_SBX) << (32U - REDUCT_INST_WIDTH_SBX) >> \
        (32U - REDUCT_INST_WIDTH_SBX))

/**
 * @brief Set the A operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _a A operand value.
 */
#define REDUCT_INST_SET_A(_inst, _a) \
    (((_inst) & ~(REDUCT_INST_MASK_A << REDUCT_INST_POS_A)) | (((_a) & REDUCT_INST_MASK_A) << REDUCT_INST_POS_A))

/**
 * @brief Set the B operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _b B operand value.
 */
#define REDUCT_INST_SET_B(_inst, _b) \
    (((_inst) & ~(REDUCT_INST_MASK_B << REDUCT_INST_POS_B)) | (((_b) & REDUCT_INST_MASK_B) << REDUCT_INST_POS_B))

/**
 * @brief Set the C operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _c C operand value.
 */
#define REDUCT_INST_SET_C(_inst, _c) \
    (((_inst) & ~(REDUCT_INST_MASK_C << REDUCT_INST_POS_C)) | (((_c) & REDUCT_INST_MASK_C) << REDUCT_INST_POS_C))

/**
 * @brief Set the SBX operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _sbx SBX operand value.
 */
#define REDUCT_INST_SET_SBX(_inst, _sbx) \
    (((_inst) & ~(REDUCT_INST_MASK_SBX << REDUCT_INST_POS_B)) | (((_sbx) & REDUCT_INST_MASK_SBX) << REDUCT_INST_POS_B))

#endif
