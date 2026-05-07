#ifndef REDUCT_FUNCTION_H
#define REDUCT_FUNCTION_H 1

#include "reduct/inst.h"
#include "reduct/defs.h"

struct reduct;
struct reduct_item;
struct reduct_atom;

#include <assert.h>
#include <stdlib.h>

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
    REDUCT_CONST_SLOT_NONE,    ///< No constant slot.
    REDUCT_CONST_SLOT_ITEM,    ///< A constant slot containing an item.
    REDUCT_CONST_SLOT_CAPTURE, ///< A constant slot containing a variable name to be captured.
} reduct_const_slot_type_t;

/**
 * @brief Constant slot.
 * @struct reduct_const_slot_t
 */
typedef struct reduct_const_slot
{
    reduct_const_slot_type_t type; ///< The type of the constant slot.
    union {
        uint64_t raw;
        struct reduct_item* item;    ///< The item contained in the constant slot.
        struct reduct_atom* capture; ///< The name of the variable to be captured.
    };
} reduct_const_slot_t;

/**
 * @brief Create a constant slot containing an item.
 *
 * @param _item The item to wrap in a slot.
 */
#define REDUCT_CONST_SLOT_ITEM(_item) ((reduct_const_slot_t){.type = REDUCT_CONST_SLOT_ITEM, .item = (_item)})

/**
 * @brief Create a constant slot containing a variable name to be captured.
 *
 * @param _capture The name of the variable to be captured.
 */
#define REDUCT_CONST_SLOT_CAPTURE(_capture) \
    ((reduct_const_slot_t){.type = REDUCT_CONST_SLOT_CAPTURE, .capture = (_capture)})

/**
 * @brief Constant index type.
 * @typedef reduct_const_t
 */
typedef uint16_t reduct_const_t;

/**
 * @brief Function flags.
 * @typedef reduct_function_flags_t
 */
typedef enum reduct_function_flags 
{
    REDUCT_FUNCTION_FLAG_NONE = 0,
    REDUCT_FUNCTION_FLAG_VARIADIC = 1 << 0, ///< Function accepts variadic arguments.
} reduct_function_flags_t;

/**
 * @brief Compiled function structure.
 * @struct reduct_function_t
 */
typedef struct reduct_function
{
    uint32_t instCount;        ///< Number of instructions.
    uint32_t instCapacity;     ///< Capacity of the instruction array.
    reduct_inst_t* insts;             ///< An array of instructions.
    uint32_t* positions;       ///< An array of source positions parallel to the instructions.
    reduct_const_slot_t* constants;   ///< The array of constant slots forming the constant template.
    uint16_t constantCount;    ///< Number of constants.
    uint16_t constantCapacity; ///< Capacity of the constant array.
    uint16_t registerCount;    ///< The number of registers the function uses.
    uint8_t arity;             ///< The number of arguments the function expects.
    reduct_function_flags_t flags;    ///< The function flags.
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
 * @brief Get the index of a constant in a function's constant template, adding it if it doesn't exist.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param func The function.
 * @param slot The constant slot to add or lookup.
 * @return The index of the constant in the constant template.
 */
REDUCT_API reduct_const_t reduct_function_lookup_constant(struct reduct* reduct, reduct_function_t* func,
    reduct_const_slot_t* slot);

/** @} */

#endif
