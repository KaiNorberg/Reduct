#ifndef REDUCT_FUNCTION_H
#define REDUCT_FUNCTION_H 1

#include <reduct/defs.h>
#include <reduct/inst.h>
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
 * A function's constant pool is actually a template of constant slots. These slots can either
 * contain ann item or a variable name that needs to be captured from the enclosing scope when a closure is created.
 *
 * @{
 */

/**
 * @brief Constant slot type.
 * @typedef reduct_const_slot_type_t
 */
typedef enum
{
    REDUCT_CONST_SLOT_TYPE_NONE,    ///< No constant slot.
    REDUCT_CONST_SLOT_TYPE_STATIC,  ///< A constant slot containing a static value.
    REDUCT_CONST_SLOT_TYPE_CAPTURE, ///< A constant slot acting as a placeholder for a capture.
} reduct_const_slot_type_t;

/**
 * @brief Constant slot.
 * @struct reduct_const_slot_t
 */
typedef struct reduct_const_slot
{
    reduct_const_slot_type_t type; ///< The type of the constant slot.
    reduct_handle_t handle;        ///< The static handle, or `NIL` for captures.
} reduct_const_slot_t;

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
 * @brief Compiled function structure.
 * @struct reduct_function_t
 */
typedef struct reduct_function
{
    uint32_t instCount;             ///< Number of instructions.
    uint32_t instCapacity;          ///< Capacity of the instruction array.
    reduct_inst_t* insts;           ///< An array of instructions.
    uint32_t* positions;            ///< An array of source positions parallel to the instructions.
    reduct_const_slot_t* constants; ///< The array of constant slots forming the constant template.
    uint16_t constantCount;         ///< Number of constants.
    uint16_t constantCapacity;      ///< Capacity of the constant array.
    uint16_t registerCount;         ///< The number of registers the function uses.
    uint8_t arity;                  ///< The number of arguments the function expects.
    reduct_function_flags_t flags;  ///< The function flags.
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
 * @param position The position in the source code.
 */
static inline void reduct_function_emit(struct reduct* reduct, reduct_function_t* func, reduct_inst_t inst,
    uint32_t position)
{
    assert(reduct != NULL);
    assert(func != NULL);
    if (func->instCount >= func->instCapacity)
    {
        reduct_function_grow(reduct, func);
    }
    func->positions[func->instCount] = position;
    func->insts[func->instCount++] = inst;
}

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
 * @brief Add a capture placeholder slot to the function's template, returning its index.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function.
 * @return The index in the constant pool.
 */
REDUCT_API reduct_const_t reduct_function_add_capture(struct reduct* reduct, reduct_function_t* func);

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
