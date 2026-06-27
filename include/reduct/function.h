#ifndef REDUCT_FUNCTION_H
#define REDUCT_FUNCTION_H 1

#include <reduct/defs.h>
#include <reduct/inst.h>
#include <reduct/module.h>
#include <reduct/optimize.h>

#include <assert.h>
#include <stdlib.h>

struct reduct;
struct reduct_item;
struct reduct_atom;

/**
 * @file function.h
 * @brief Compiled function
 * @defgroup function Reduct function
 *
 * A reduct function is a sequence of instructions and an associated constant pool that can be executed by the Reduct
 * virtual machine.
 *
 * ## Constants Template
 *
 * A function's constant template is split into two contiguous regions and describe how a closures constant pool should
 * be created.
 *
 * The inital `captureCount` number of constant slots are for captured variables and are specified in the same order as
 * the RVSDG lambda nodes inputs. These are not actually stored physically within the function but instead will be
 * created as empty constants within created closures which are then filled using capture opcodes.
 *
 * After the captured constants are a `constantCount` number of static constants which will be copied into the closure's
 * constant pool.
 *
 * @{
 */

/**
 * @brief Function flags.
 * @typedef reduct_function_flags_t
 */
typedef enum reduct_function_flags
{
    REDUCT_FUNCTION_FLAG_NONE = 0,
    REDUCT_FUNCTION_FLAG_VARIADIC = 1 << 0,  ///< Function accepts variadic arguments.
    REDUCT_FUNCTION_FLAG_OPTIMIZED = 1 << 1, ///< Function has been optimized.
} reduct_function_flags_t;

/**
 * @brief Source position of a parallel instruction.
 * @struct reduct_function_inst_source_t
 */
typedef struct
{
    uint32_t modulePos;          ///< The index within the modules buffer that created the parallel instruction.
    reduct_module_id_t moduleId; ///< The ID of the module that the parallel instruction is associated with.
} reduct_function_inst_source_t;

/**
 * @brief Compiled function structure.
 * @struct reduct_function_t
 */
typedef struct reduct_function
{
    uint32_t instCount;                     ///< Number of instructions.
    uint32_t instCapacity;                  ///< Capacity of the instruction array.
    reduct_inst_t* insts;                   ///< An array of instructions.
    reduct_function_inst_source_t* sources; ///< An array specifying the source of instructions.
    reduct_handle_t* constants;             ///< Static constant values. Slot `i` is `captureCount + i`.
    uint16_t constantCount;                 ///< Number of static constants.
    uint16_t constantCapacity;              ///< Capacity of the constants array.
    uint16_t captureCount;                  ///< Number of captured-variable slots.
    uint16_t registerCount;                 ///< The number of registers the function uses.
    uint8_t arity;                          ///< The number of arguments the function expects.
    reduct_function_flags_t flags;          ///< The function flags.
} reduct_function_t;

/**
 * @brief Initialize a function structure.
 *
 * @param func The function to initialize.
 */
REDUCT_API void reduct_function_init(reduct_function_t* func);

/**
 * @brief Create a new function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return A pointer to the newly allocated function.
 */
REDUCT_API reduct_function_t* reduct_function_new(struct reduct* reduct);

/**
 * @brief Grow the instruction buffer.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function to grow.
 */
REDUCT_API void reduct_function_grow(struct reduct* reduct, reduct_function_t* func);

/**
 * @brief Emit an instruction to the function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function to emit to.
 * @param inst The instruction to emit.
 * @param modulePos The index within the modules buffer that created the parallel instruction.
 * @param moduleId The ID of the module that the parallel instruction is associated with.
 */
static inline void reduct_function_emit(struct reduct* reduct, reduct_function_t* func, reduct_inst_t inst,
    uint32_t modulePos, reduct_module_id_t moduleId)
{
    assert(reduct != NULL);
    assert(func != NULL);
    if (func->instCount >= func->instCapacity)
    {
        reduct_function_grow(reduct, func);
    }
    func->sources[func->instCount].modulePos = modulePos;
    func->sources[func->instCount].moduleId = moduleId;
    func->insts[func->instCount++] = inst;
}

/**
 * @brief Set the number of captured variables for a function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function to modify.
 * @param captureCount The new number of captured variables.
 */
REDUCT_API void reduct_function_set_capture_count(struct reduct* reduct, reduct_function_t* func,
    uint16_t captureCount);

/**
 * @brief Add a static constant to the function's template, returning its index.
 *
 * Will return the index of an existing identical constant if found.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function.
 * @param handle The value to add.
 * @return The index in the constant pool.
 */
REDUCT_API reduct_const_t reduct_function_add_constant(struct reduct* reduct, reduct_function_t* func,
    reduct_handle_t handle);

/**
 * @brief Retain a function, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param function Pointer to the function, can be `NULL`.
 */
REDUCT_API void reduct_function_retain(struct reduct* reduct, reduct_function_t* function);

/**
 * @brief Release a function, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param function Pointer to the function, can be `NULL`.
 */
REDUCT_API void reduct_function_release(struct reduct* reduct, reduct_function_t* function);

/** @} */

#endif
