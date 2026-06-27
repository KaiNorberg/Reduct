#ifndef REDUCT_INST_H
#define REDUCT_INST_H 1

#include <reduct/defs.h>
#include <reduct/opcode.h>

/**
 * @file inst.h
 * @brief Bytecode instruction format.
 * @defgroup inst Instruction Format
 *
 * Instructions are 32-bit words with the following formats:
 *
 * - iABC:  [ Opcode:8 | A:8 | B:8 | C:8 ]
 * - iSAxC: [ Opcode:8 | sAx:16 | C:8 ]
 *
 * Fields:
 * - A: Usually the target/destination register.
 * - B: Usually the first operand (register).
 * - C: Usually the second operand (register or constant).
 * - sAx: Signed offsets for jumps.
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
 * @brief Constant index type.
 * @typedef reduct_const_t
 */
typedef uint16_t reduct_const_t;

/**
 * @brief Invalid constant value.
 */
#define REDUCT_CONST_INVALID ((reduct_const_t) - 1)

/**
 * @brief Register type.
 */
typedef uint16_t reduct_reg_t;

/**
 * @brief Invalid register value.
 */
#define REDUCT_REGISTER_INVALID ((reduct_reg_t) - 1)

/**
 * @brief Instruction type.
 * @typedef reduct_inst_t
 */
typedef uint32_t reduct_inst_t;

/**
 * @brief Check if an instruction reads from a specific register.
 *
 * @param _inst The instruction.
 * @param _reg The register index.
 */
#define REDUCT_INST_READS_REG(_inst, _reg) \
    ((REDUCT_OPCODE_READS_A(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_A(_inst)) || \
        (REDUCT_OPCODE_READS_B(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_B(_inst)) || \
        (REDUCT_OPCODE_READS_C(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_C(_inst)) || \
        (REDUCT_OPCODE_READS_RANGE(REDUCT_INST_GET_OP(_inst)) && (_reg) >= REDUCT_INST_GET_A(_inst) && \
            (_reg) < REDUCT_INST_GET_A(_inst) + REDUCT_INST_GET_B(_inst)))

/**
 * @brief Check if an instruction writes to a specific register.
 *
 * @param _inst The instruction.
 * @param _reg The register index.
 */
#define REDUCT_INST_WRITES_REG(_inst, _reg) \
    (REDUCT_OPCODE_HAS_TARGET(REDUCT_INST_GET_OP(_inst)) && (_reg) == REDUCT_INST_GET_A(_inst))

#define REDUCT_INST_WIDTH_OPCODE 8U                                       ///< Opcode width in bits.
#define REDUCT_INST_WIDTH_A 8U                                            ///< A operand width in bits.
#define REDUCT_INST_WIDTH_B 8U                                            ///< B operand width in bits.
#define REDUCT_INST_WIDTH_C 8U                                            ///< C operand width in bits.
#define REDUCT_INST_WIDTH_SAX (REDUCT_INST_WIDTH_A + REDUCT_INST_WIDTH_B) ///< SAx operand width in bits.

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
#define REDUCT_INST_POS_SAX (REDUCT_INST_POS_A)                               ///< SAx operand position in bits.

#define REDUCT_INST_MASK_OPCODE ((1U << REDUCT_INST_WIDTH_OPCODE) - 1U) ///< Opcode mask.
#define REDUCT_INST_MASK_A ((1U << REDUCT_INST_WIDTH_A) - 1U)           ///< A operand mask.
#define REDUCT_INST_MASK_B ((1U << REDUCT_INST_WIDTH_B) - 1U)           ///< B operand mask.
#define REDUCT_INST_MASK_C ((1U << REDUCT_INST_WIDTH_C) - 1U)           ///< C operand mask.
#define REDUCT_INST_MASK_SAX ((1U << REDUCT_INST_WIDTH_SAX) - 1U)       ///< SAx operand mask.

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
 * @brief Create an instruction with opcode and SAx operand, and C operand.
 *
 * @param _op Opcode operand.
 * @param _sax SAx operand.
 * @param _c C operand.
 */
#define REDUCT_INST_MAKE_SAXC(_op, _sax, _c) \
    ((((reduct_inst_t)(_op)) & REDUCT_INST_MASK_OPCODE) << REDUCT_INST_POS_OPCODE | \
        (((reduct_inst_t)(_sax)) & REDUCT_INST_MASK_SAX) << REDUCT_INST_POS_A | \
        (((reduct_inst_t)(_c)) & REDUCT_INST_MASK_C) << REDUCT_INST_POS_C)

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
 * @brief Get the SAX operand from an instruction.
 *
 * @param _inst Instruction.
 */
#define REDUCT_INST_GET_SAX(_inst) ((int32_t)(int16_t)(((_inst) >> REDUCT_INST_POS_SAX) & REDUCT_INST_MASK_SAX))

/**
 * @brief Set the opcode in an instruction.
 *
 * @param _inst Instruction.
 * @param _op Opcode value.
 */
#define REDUCT_INST_SET_OP(_inst, _op) \
    (((_inst) & ~(REDUCT_INST_MASK_OPCODE << REDUCT_INST_POS_OPCODE)) | \
        (((_op) & REDUCT_INST_MASK_OPCODE) << REDUCT_INST_POS_OPCODE))

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
 * @brief Set the SAX operand in an instruction.
 *
 * @param _inst Instruction.
 * @param _sax SAX operand value.
 */
#define REDUCT_INST_SET_SAX(_inst, _sax) \
    (((_inst) & ~(REDUCT_INST_MASK_SAX << REDUCT_INST_POS_A)) | (((_sax) & REDUCT_INST_MASK_SAX) << REDUCT_INST_POS_A))

#endif
