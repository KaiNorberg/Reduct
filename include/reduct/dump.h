#ifndef REDUCT_DUMP_H
#define REDUCT_DUMP_H 1

#include <reduct/core.h>

/**
 * @file dump.h
 * @brief Dumping and disassembly.
 * @defgroup dump Dumping
 *
 * @{
 */

/**
 * @brief Dump a compiled function to a file stream.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param function Handle to the compiled function.
 * @param out The output file stream to write the disassembly to.
 */
REDUCT_API void reduct_dump_function(reduct_t* reduct, reduct_handle_t function, FILE* out);

/**
 * @brief Dump an IR graph to a file stream.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param graph Pointer to the IR graph.
 * @param out The output file stream.
 */
REDUCT_API void reduct_dump_rvsdg(struct reduct* reduct, reduct_handle_t graph, FILE* out);

/** @} */

#endif
