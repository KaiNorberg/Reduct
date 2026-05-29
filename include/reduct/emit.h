#ifndef REDUCT_EMIT_H
#define REDUCT_EMIT_H 1

#include <reduct/bitmap.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/item.h>
#include <reduct/list.h>

/**
 * @file emit.h
 * @brief Bytecode Emission
 * @defgroup emit Emission
 *
 * The emitter converts IR into register-based bytecode that can be executed by the Reduct virtual machine / evaluator.
 *
 * @{
 */

/**
 * @brief Compile Reduct IR into a callable bytecode function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param graph Handle to the root of the IR graph.
 * @return Handle to the compiled function.
 */
REDUCT_API reduct_handle_t reduct_emit(struct reduct* reduct, reduct_handle_t graph);

/** @} */

#endif
