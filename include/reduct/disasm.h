#ifndef REDUCT_DISASM_H
#define REDUCT_DISASM_H 1

#include "reduct/compile.h"
#include "reduct/core.h"

/**
 * @file disasm.h
 * @brief Bytecode disassembly.
 * @defgroup disasm Disassembly
 *
 * @{
 */

/**
 * @brief Disassembles a compiled function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param function Handle to the compiled function.
 * @param out The output file stream to write the disassembly to.
 */
REDUCT_API void reduct_disasm(reduct_t* reduct, reduct_handle_t function, FILE* out);

/** @} */

#endif
