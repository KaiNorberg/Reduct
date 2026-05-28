#ifndef REDUCT_OPCODE_H
#define REDUCT_OPCODE_H 1

#include <reduct/defs.h>
#include <stdint.h>

/**
 * @file opcode.h
 * @brief Bytecode opcodes and properties.
 * @defgroup opcode Opcodes
 * @{
 */

/**
 * @brief Opcode mode enumeration.
 * @enum reduct_opcode_mode_t
 */
typedef enum
{
    REDUCT_OPCODE_MODE_REG = 0,        ///< Register operand mode.
    REDUCT_OPCODE_MODE_CONST = 1 << 7, ///< Constant operand mode.
} reduct_opcode_mode_t;

/**
 * @brief Opcode enumeration.
 * @enum reduct_opcode_t
 */
typedef enum
{
    REDUCT_OPCODE_NOP = 0,   ///< No operation.
    REDUCT_OPCODE_MOV,       ///< (A, C) Move value in R/K(C) to R(A).
    REDUCT_OPCODE_LIST,      ///< (A, B) R(A) = (R(A) R(A + 1) ... R(A + B - 1))
    REDUCT_OPCODE_CLOSURE,   ///< (A, C) Wrap the function prototype in K(C) in a closure and store in R(A).
    REDUCT_OPCODE_CAPTURE,   ///< (A, B, C) Capture R/K(C) into constant slot B in closure R(A).
    REDUCT_OPCODE_JMP,       ///< (sAx) Unconditional jump by relative offset sAx.
    REDUCT_OPCODE_JMPF,      ///< (sAx, C) Jump by sAx if R(C) is falsy.
    REDUCT_OPCODE_JMPT,      ///< (sAx, C) Jump by sAx if R(C) is truthy.
    REDUCT_OPCODE_CALL,      ///< (A, B, C) Call callable in R/K(C) with B args starting from R(A). Result in R(A).
    REDUCT_OPCODE_RET,       ///< (C) Return value in R/K(C).
    REDUCT_OPCODE_TAILCALL,  ///< (A, B, C) Tail call callable in R/K(C) with B args starting from R(A).
    REDUCT_OPCODE_RECUR,     ///< (A, B) Recursively call the current function with B args starting from R(A).
    REDUCT_OPCODE_TAILRECUR, ///< (A, B) Recursively tail call the current function with B args starting from R(A).
    REDUCT_OPCODE_EQ,        ///< (A, B, C) If R(B) == R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_NEQ,       ///< (A, B, C) If R(B) != R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LT,        ///< (A, B, C) If R(B) < R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_LE,        ///< (A, B, C) If R(B) <= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GT,        ///< (A, B, C) If R(B) > R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_GE,        ///< (A, B, C) If R(B) >= R/K(C) store true in R(A), else false.
    REDUCT_OPCODE_ADD,       ///< (A, B, C) R(A) = R(B) + R/K(C)
    REDUCT_OPCODE_SUB,       ///< (A, B, C) R(A) = R(B) - R/K(C)
    REDUCT_OPCODE_MUL,       ///< (A, B, C) R(A) = R(B) * R/K(C)
    REDUCT_OPCODE_DIV,       ///< (A, B, C) R(A) = R(B) / R/K(C)
    REDUCT_OPCODE_MOD,       ///< (A, B, C) R(A) = R(B) % R/K(C)
    REDUCT_OPCODE_BAND,      ///< (A, B, C) R(A) = R(B) & R/K(C)
    REDUCT_OPCODE_BOR,       ///< (A, B, C) R(A) = R(B) | R/K(C)
    REDUCT_OPCODE_BXOR,      ///< (A, B, C) R(A) = R(B) ^ R/K(C)
    REDUCT_OPCODE_BNOT,      ///< (A, C) R(A) = ~R/K(C)
    REDUCT_OPCODE_SHL,       ///< (A, B, C) R(A) = R(B) << R/K(C)
    REDUCT_OPCODE_SHR,       ///< (A, B, C) R(A) = R(B) >> R/K(C)
    REDUCT_OPCODE_JEQ,       ///< (B, C) Skip the next instruction if R(B) == R/K(C), else continue.
    REDUCT_OPCODE_JNEQ,      ///< (B, C) Skip the next instruction if R(B) != R/K(C), else continue.
    REDUCT_OPCODE_JLT,       ///< (B, C) Skip the next instruction if R(B) < R/K(C), else continue.
    REDUCT_OPCODE_JLE,       ///< (B, C) Skip the next instruction if R(B) <= R/K(C), else continue.
    REDUCT_OPCODE_JGT,       ///< (B, C) Skip the next instruction if R(B) > R/K(C), else continue.
    REDUCT_OPCODE_JGE,       ///< (B, C) Skip the next instruction if R(B) >= R/K(C), else continue.
    REDUCT_OPCODE_MOV_CONST = REDUCT_OPCODE_MOV | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_CALL_CONST = REDUCT_OPCODE_CALL | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_RET_CONST = REDUCT_OPCODE_RET | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_CAPTURE_CONST = REDUCT_OPCODE_CAPTURE | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_TAILCALL_CONST = REDUCT_OPCODE_TAILCALL | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_EQ_CONST = REDUCT_OPCODE_EQ | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_NEQ_CONST = REDUCT_OPCODE_NEQ | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_LT_CONST = REDUCT_OPCODE_LT | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_LE_CONST = REDUCT_OPCODE_LE | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_GT_CONST = REDUCT_OPCODE_GT | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_GE_CONST = REDUCT_OPCODE_GE | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_ADD_CONST = REDUCT_OPCODE_ADD | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_SUB_CONST = REDUCT_OPCODE_SUB | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_MUL_CONST = REDUCT_OPCODE_MUL | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_DIV_CONST = REDUCT_OPCODE_DIV | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_MOD_CONST = REDUCT_OPCODE_MOD | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_BAND_CONST = REDUCT_OPCODE_BAND | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_BOR_CONST = REDUCT_OPCODE_BOR | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_BXOR_CONST = REDUCT_OPCODE_BXOR | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_BNOT_CONST = REDUCT_OPCODE_BNOT | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_SHL_CONST = REDUCT_OPCODE_SHL | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_SHR_CONST = REDUCT_OPCODE_SHR | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JEQ_CONST = REDUCT_OPCODE_JEQ | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JNEQ_CONST = REDUCT_OPCODE_JNEQ | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JLT_CONST = REDUCT_OPCODE_JLT | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JLE_CONST = REDUCT_OPCODE_JLE | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JGT_CONST = REDUCT_OPCODE_JGT | REDUCT_OPCODE_MODE_CONST,
    REDUCT_OPCODE_JGE_CONST = REDUCT_OPCODE_JGE | REDUCT_OPCODE_MODE_CONST,
} reduct_opcode_t;

#define REDUCT_OPCODE_FLAG_HAS_TARGET (1 << 0)     ///< Opcode modifies target register A.
#define REDUCT_OPCODE_FLAG_IS_JUMP (1 << 1)        ///< Opcode is a jump.
#define REDUCT_OPCODE_FLAG_HAS_CONST (1 << 2)      ///< Opcode uses C operand and has both reg/const versions.
#define REDUCT_OPCODE_FLAG_READ_A (1 << 3)         ///< Opcode reads from register A (or range starting at A).
#define REDUCT_OPCODE_FLAG_READ_B (1 << 4)         ///< Opcode reads from register B.
#define REDUCT_OPCODE_FLAG_READ_C (1 << 5)         ///< Opcode reads from register/constant C.
#define REDUCT_OPCODE_FLAG_READ_RANGE (1 << 6)     ///< Opcode reads a range of registers starting at A.
#define REDUCT_OPCODE_FLAG_IS_COMMUTATIVE (1 << 7) ///< Opcode is commutative.
#define REDUCT_OPCODE_FLAG_IS_SKIP (1 << 8)        ///< Opcode is a skip instruction.
#define REDUCT_OPCODE_FLAG_IS_CALL (1 << 9)        ///< Opcode is a function call.
#define REDUCT_OPCODE_FLAG_IS_TERMINATOR (1 << 10) ///< Opcode ends basic block reachability.
#define REDUCT_OPCODE_FLAG_IS_RECUR (1 << 11)      ///< Opcode is a recursive call.

/**
 * @brief Opcode flags lookup table.
 */
REDUCT_API extern const uint16_t reductOpcodeFlags[UINT8_MAX + 1];

/**
 * @brief Get the base form of the opcode without the constant bit set.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_BASE(_op) ((_op) & ~REDUCT_OPCODE_MODE_CONST)

/**
 * @brief Check if an opcode is a comparison instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_COMPARE(_op) \
    (REDUCT_OPCODE_BASE(_op) >= REDUCT_OPCODE_EQ && REDUCT_OPCODE_BASE(_op) <= REDUCT_OPCODE_GE)

/**
 * @brief Get the skip version of a comparison opcode.
 *
 * @param _op The comparison opcode.
 */
#define REDUCT_OPCODE_TO_SKIP(_op) \
    (REDUCT_OPCODE_IS_COMPARE(_op) ? (reduct_opcode_t)((_op) + (REDUCT_OPCODE_JEQ - REDUCT_OPCODE_EQ)) : REDUCT_OPCODE_NOP)

/**
 * @brief Get the recursive version of a call opcode.
 *
 * @param _op The call opcode.
 */
#define REDUCT_OPCODE_TO_RECUR(_op) \
    (REDUCT_OPCODE_IS_CALL(_op) \
            ? (reduct_opcode_t)((REDUCT_OPCODE_BASE(_op) == REDUCT_OPCODE_TAILCALL) ? REDUCT_OPCODE_TAILRECUR \
                                                                                   : REDUCT_OPCODE_RECUR) \
            : REDUCT_OPCODE_NOP)

/**
 * @brief Get the tail version of a call or return opcode.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_TO_TAIL(_op) \
    ((_op) == REDUCT_OPCODE_CALL ? REDUCT_OPCODE_TAILCALL : \
     (_op) == REDUCT_OPCODE_CALL_CONST ? REDUCT_OPCODE_TAILCALL_CONST : \
     (_op) == REDUCT_OPCODE_RECUR ? REDUCT_OPCODE_TAILRECUR : (_op))

/**
 * @brief Check if an opcode modifies its target register (A).
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_HAS_TARGET(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_HAS_TARGET)

/**
 * @brief Check if an opcode is a recursive call.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_RECUR(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_RECUR)

/**
 * @brief Check if an opcode is a jump instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_JUMP(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_JUMP)

/**
 * @brief Check if an opcode uses the C operand and has both a constant and register version.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_HAS_CONST(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_HAS_CONST)

/**
 * @brief Check if an opcode reads from register A.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_A(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_READ_A)

/**
 * @brief Check if an opcode reads from register B.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_B(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_READ_B)

/**
 * @brief Check if an opcode reads from register/constant C.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_C(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_READ_C)

/**
 * @brief Check if an opcode reads a range of registers starting at A.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_READS_RANGE(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_READ_RANGE)

/**
 * @brief Check if an opcode is commutative.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_COMMUTATIVE(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_COMMUTATIVE)

/**
 * @brief Check if an opcode is a skip instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_SKIP(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_SKIP)

/**
 * @brief Check if an opcode is a skip or jump instruction.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_BRANCH(_op) \
    (reductOpcodeFlags[(_op) & 0xFF] & (REDUCT_OPCODE_FLAG_IS_SKIP | REDUCT_OPCODE_FLAG_IS_JUMP))

/**
 * @brief Check if an opcode is a function call.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_CALL(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_CALL)

/**
 * @brief Check if an opcode is a terminator.
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_TERMINATOR(_op) (reductOpcodeFlags[(_op) & 0xFF] & REDUCT_OPCODE_FLAG_IS_TERMINATOR)

/**
 * @brief Check if an opcode is a binary operation (reads B and C).
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_BINARY(_op) \
    ((reductOpcodeFlags[(_op) & 0xFF] & (REDUCT_OPCODE_FLAG_READ_B | REDUCT_OPCODE_FLAG_READ_C)) == \
        (REDUCT_OPCODE_FLAG_READ_B | REDUCT_OPCODE_FLAG_READ_C))

/**
 * @brief Check if an opcode is a unary operation (reads C only).
 *
 * @param _op The opcode.
 */
#define REDUCT_OPCODE_IS_UNARY(_op) \
    ((reductOpcodeFlags[(_op) & 0xFF] & (REDUCT_OPCODE_FLAG_READ_B | REDUCT_OPCODE_FLAG_READ_C)) == \
        REDUCT_OPCODE_FLAG_READ_C)

/** @} */

#endif
