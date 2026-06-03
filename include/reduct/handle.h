#ifndef REDUCT_HANDLE_H
#define REDUCT_HANDLE_H 1

#include <reduct/atom.h>
#include <reduct/defs.h>
#include <reduct/error.h>
#include <reduct/item.h>
#include <reduct/standard.h>

#include <math.h>

struct reduct;

/**
 * @file handle.h
 * @brief Handle management.
 * @defgroup handle Handle
 *
 * A handle is a lightweight reference to a Reduct item, with the ability to cache a numeric value or reference
 * an item using Tagged Pointers (NaN Boxing).
 *
 * ## 64-bit Handle Bit Layout
 *
 * The top 16 bits are used as a type tag. The remaining 48 bits represent the payload
 * (either a 48-bit pointer or a shifted IEEE 754 double).
 *
 * | Tag (16 bits)   | Payload (48 bits)                 |
 * |-----------------|-----------------------------------|
 * | `0x0000`        | Item Pointer (`reduct_item_t*`)   |
 * | `0x0007...FFFF` | Number (Shifted IEEE 754 double)  |
 *
 * @see [Wikipedia Tagged pointer](https://en.wikipedia.org/wiki/Tagged_pointer)
 *
 * @{
 */

/**
 * @brief High-level handle types.
 * @enum reduct_handle_type_t
 */
typedef enum
{
    REDUCT_HANDLE_TYPE_NONE = 0,   ///< Invalid type.
    REDUCT_HANDLE_TYPE_NUMBER,     ///< Handle is a number or references a number shaped atom.
    REDUCT_HANDLE_TYPE_ATOM,       ///< Handle is a reference to an atom.
    REDUCT_HANDLE_TYPE_LIST,       ///< Handle is a reference to a list.
    REDUCT_HANDLE_TYPE_FUNCTION,   ///< Handle is a reference to a function.
    REDUCT_HANDLE_TYPE_CLOSURE,    ///< Handle is a reference to a closure.
    REDUCT_HANDLE_TYPE_ATOM_STACK, ///< Handle is a reference to an atom stack.
    REDUCT_HANDLE_TYPE_LIST_NODE,  ///< Handle is a reference to a list node.
    REDUCT_HANDLE_TYPE_RVSDG_NODE, ///< Handle is a reference to an IR node.
    REDUCT_HANDLE_TYPE_RVSDG_EDGE, ///< Handle is a reference to an IR edge.
    REDUCT_HANDLE_TYPE_FUTURE,     ///< Handle is a reference to a future.
    REDUCT_HANDLE_TYPE_UNKNOWN     ///< Handle is corrupt or otherwise invalid.
} reduct_handle_type_t;

#define REDUCT_HANDLE_OFFSET_NUMBER 0x0007000000000000ULL ///< Offset used for encoding doubles.

#define REDUCT_HANDLE_TAG_ITEM 0x0000000000000000ULL ///< Tag for item handles.

#define REDUCT_HANDLE_MASK_TAG 0xFFFF000000000000ULL  ///< Mask for handle tag bits.
#define REDUCT_HANDLE_MASK_VAL 0x0000FFFFFFFFFFFFULL  ///< Mask for handle value bits.
#define REDUCT_HANDLE_MASK_PTR REDUCT_HANDLE_MASK_VAL ///< Mask for item pointer bits.

#define REDUCT_HANDLE_NUMBER_WIDTH 64 ///< The bit width used for shift operations on numbers.

/**
 * @brief Create a handle from a number.
 *
 * @param _val The number value.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_NUMBER(_val) \
    ((reduct_handle_t){((union { \
        double d; \
        uint64_t u; \
    }){.d = (_val)}) \
                           .u + \
        REDUCT_HANDLE_OFFSET_NUMBER})

/**
 * @brief Create a handle from an item pointer.
 *
 * @param _ptr The pointer to the reduct_item_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_ITEM(_ptr) \
    ((reduct_handle_t){REDUCT_HANDLE_TAG_ITEM | ((uintptr_t)(void*)(_ptr) & REDUCT_HANDLE_MASK_PTR)})

/**
 * @brief Create a handle from an atom pointer.
 *
 * @param _atom The pointer to the reduct_atom_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_ATOM(_atom) REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_atom, reduct_item_t, atom))

/**
 * @brief Create a handle from a list pointer.
 *
 * @param _list The pointer to the reduct_list_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_LIST(_list) REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_list, reduct_item_t, list))

/**
 * @brief Create a handle from a function pointer.
 *
 * @param _func The pointer to the reduct_function_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_FUNCTION(_func) REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_func, reduct_item_t, function))

/**
 * @brief Create a handle from a closure pointer.
 *
 * @param _closure The pointer to the reduct_closure_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_CLOSURE(_closure) \
    REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_closure, reduct_item_t, closure))

/**
 * @brief Create a handle from an IR node pointer.
 *
 * @param _node The pointer to the reduct_rvsdg_node_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_RVSDG_NODE(_node) \
    REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_node, reduct_item_t, rvsdgNode))

/**
 * @brief Create a handle from an IR edge pointer.
 *
 * @param _edge The pointer to the reduct_rvsdg_edge_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_RVSDG_EDGE(_edge) \
    REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_edge, reduct_item_t, rvsdgEdge))

/**
 * @brief Create a boolean handle from a C condition.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _cond The condition to evaluate.
 */
#define REDUCT_HANDLE_FROM_BOOL(_reduct, _cond) ((_cond) ? REDUCT_HANDLE_TRUE() : REDUCT_HANDLE_FALSE(_reduct))

/**
 * @brief Create a handle from a future pointer.
 * 
 * @param _future The pointer to the reduct_future_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_FUTURE(_future) \
    REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(_future, reduct_item_t, future))

/**
 * @brief Check if a handle is a number.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a number, zero otherwise.
 */
#define REDUCT_HANDLE_IS_NUMBER(_handle) (((_handle)._value) >= REDUCT_HANDLE_OFFSET_NUMBER)

/**
 * @brief Check if a handle is a number or references a number shaped item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is number shaped, zero otherwise.
 */
#define REDUCT_HANDLE_IS_NUMBER_SHAPED(_handle) \
    (REDUCT_HANDLE_IS_NUMBER(_handle) || (REDUCT_HANDLE_IS_ATOM(_handle) && \
        reduct_atom_is_number(REDUCT_HANDLE_TO_ATOM(_handle))))

/**
 * @brief Get the high-level type of a handle.
 *
 * @param handle Pointer to the handle.
 * @return The type of the handle.
 */
REDUCT_API reduct_handle_type_t reduct_handle_get_type(reduct_handle_t handle);

/**
 * @brief Get the string name of a handle type.
 *
 * @param type The handle type.
 * @return A constant string representing the type.
 */
REDUCT_API const char* reduct_handle_type_string(reduct_handle_type_t type);

/**
 * @brief Get the string representation of the type of the item referenced by the handle.
 *
 * @param _handle Pointer to the handle.
 * @return The string representation of the item type.
 */
#define REDUCT_HANDLE_GET_TYPE_STRING(_handle) (reduct_handle_type_string(reduct_handle_get_type(_handle)))

/**
 * @brief Check if a handle is nil (an empty list).
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is nil, zero otherwise.
 */
#define REDUCT_HANDLE_IS_NIL(_handle) (REDUCT_HANDLE_IS_LIST(_handle) && REDUCT_HANDLE_TO_LIST(_handle)->length == 0)

/**
 * @brief Check if a handle is empty (nil or empty atom).
 *
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_IS_EMPTY(_handle) \
    (REDUCT_HANDLE_IS_NIL(_handle) || (REDUCT_HANDLE_IS_ATOM(_handle) && REDUCT_HANDLE_TO_ATOM(_handle)->length == 0))

/**
 * @brief Check if a handle is an item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an item, zero otherwise.
 */
#define REDUCT_HANDLE_IS_ITEM(_handle) ((((_handle)._value) & REDUCT_HANDLE_MASK_TAG) == REDUCT_HANDLE_TAG_ITEM)

/**
 * @brief Check if a handle is an atom.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an atom, zero otherwise.
 */
#define REDUCT_HANDLE_IS_ATOM(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_ATOM)

/**
 * @brief Check if a handle is a list.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a list, zero otherwise.
 */
#define REDUCT_HANDLE_IS_LIST(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_LIST)

/**
 * @brief Check if a handle is a function.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a function, zero otherwise.
 */
#define REDUCT_HANDLE_IS_FUNCTION(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_FUNCTION)

/**
 * @brief Check if a handle is a closure.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a closure, zero otherwise.
 */
#define REDUCT_HANDLE_IS_CLOSURE(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_CLOSURE)

/**
 * @brief Check if a handle is an IR node.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an IR node, zero otherwise.
 */
#define REDUCT_HANDLE_IS_RVSDG_NODE(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_RVSDG_NODE)

/**
 * @brief Check if a handle is an IR edge.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an IR edge, zero otherwise.
 */
#define REDUCT_HANDLE_IS_RVSDG_EDGE(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_RVSDG_EDGE)

/**
 * @brief Check if a handle is a future.
 *
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_IS_FUTURE(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_TO_ITEM(_handle)->type == REDUCT_ITEM_TYPE_FUTURE)

/**
 * @brief Check if a handle is a lambda.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a lambda, zero otherwise.
 */
#define REDUCT_HANDLE_IS_LAMBDA(_handle) (REDUCT_HANDLE_IS_FUNCTION(_handle) || REDUCT_HANDLE_IS_CLOSURE(_handle))

/**
 * @brief Check if a handle is a native function.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a native function, zero otherwise.
 */
#define REDUCT_HANDLE_IS_NATIVE(_reduct, _handle) \
    (REDUCT_HANDLE_IS_ATOM(_handle) && reduct_atom_is_native(_reduct, REDUCT_HANDLE_TO_ATOM(_handle)))

/**
 * @brief Check if a handle is an intrinsic function.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an intrinsic function, zero otherwise.
 */
#define REDUCT_HANDLE_IS_INTRINSIC(_reduct, _handle) \
    (REDUCT_HANDLE_IS_ATOM(_handle) && reduct_atom_is_intrinsic(_reduct, REDUCT_HANDLE_TO_ATOM(_handle)))

/**
 * @brief Check if a handle is callable.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is callable, zero otherwise.
 */
#define REDUCT_HANDLE_IS_CALLABLE(_reduct, _handle) \
    (REDUCT_HANDLE_IS_LAMBDA(_handle) || REDUCT_HANDLE_IS_NATIVE(_reduct, _handle))

/**
 * @brief Check if a handle either is an atom, or could be represented by an atom item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is atom-like, zero otherwise.
 */
#define REDUCT_HANDLE_IS_ATOM_LIKE(_handle) (REDUCT_HANDLE_IS_NUMBER(_handle) || REDUCT_HANDLE_IS_ATOM(_handle))

/**
 * @brief Get the number value of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The number value.
 */
#define REDUCT_HANDLE_TO_NUMBER(_handle) \
    (((union { \
        uint64_t u; \
        double d; \
    }){.u = ((_handle)._value) - REDUCT_HANDLE_OFFSET_NUMBER}) \
            .d)

/**
 * @brief Get the item pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The item pointer.
 */
#define REDUCT_HANDLE_TO_ITEM(_handle) ((reduct_item_t*)(void*)(((_handle)._value) & REDUCT_HANDLE_MASK_PTR))

/**
 * @brief Get the atom pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The atom pointer.
 */
#define REDUCT_HANDLE_TO_ATOM(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->atom)

/**
 * @brief Get the list pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The list pointer.
 */
#define REDUCT_HANDLE_TO_LIST(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->list)

/**
 * @brief Get the function pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The function pointer.
 */
#define REDUCT_HANDLE_TO_FUNCTION(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->function)

/**
 * @brief Get the closure pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The closure pointer.
 */
#define REDUCT_HANDLE_TO_CLOSURE(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->closure)

/**
 * @brief Get the IR node pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The IR node pointer.
 */
#define REDUCT_HANDLE_TO_RVSDG_NODE(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->rvsdgNode)

/**
 * @brief Get the IR edge pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The IR edge pointer.
 */
#define REDUCT_HANDLE_TO_RVSDG_EDGE(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->rvsdgEdge)

/**
 * @brief Get the future pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_TO_FUTURE(_handle) (&REDUCT_HANDLE_TO_ITEM(_handle)->future)

/**
 * @brief Get the boolean value of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The boolean value.
 */
#define REDUCT_HANDLE_TO_BOOL(_handle) (REDUCT_HANDLE_IS_TRUTHY(_handle) ? true : false)

/**
 * @brief Create a list handle.
 *
 * @param _reduct Pointer to the Reduct structure.
 */
#define REDUCT_HANDLE_CREATE_LIST(_reduct) REDUCT_HANDLE_FROM_LIST(reduct_list_new(_reduct))

/**
 * @brief Create a list handle from an array of handles.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _count The number of handles.
 * @param _handles The array of handles.
 */
#define REDUCT_HANDLE_CREATE_HANDLES(_reduct, _count, _handles) \
    REDUCT_HANDLE_FROM_LIST(reduct_list_new_handles(_reduct, _count, _handles))

/**
 * @brief Create a list handle of pairs (key-value) from a variable number of pairs.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _count The number of pairs.
 * @param ... Each pair should be provided as a `(const char*, reduct_handle_t)`.
 */
#define REDUCT_HANDLE_CREATE_ALIST(_reduct, _count, ...) \
    REDUCT_HANDLE_FROM_LIST(reduct_list_new_alist(_reduct, _count, __VA_ARGS__))

/**
 * @brief Create an atom handle with a reserved size.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _len The length of the buffer.
 */
#define REDUCT_HANDLE_CREATE_ATOM(_reduct, _len) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(_reduct, _len))

/**
 * @brief Create an atom handle from a string.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _str The null-terminated string.
 */
#define REDUCT_HANDLE_CREATE_STRING(_reduct, _str) \
    REDUCT_HANDLE_FROM_ATOM(reduct_atom_lookup(_reduct, _str, strlen(_str), REDUCT_ATOM_LOOKUP_QUOTED))

/**
 * @brief Create an interned atom handle from a string.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _str The null-terminated string.
 */
#define REDUCT_HANDLE_CREATE_SYMBOL(_reduct, _str) \
    REDUCT_HANDLE_FROM_ATOM(reduct_atom_lookup(_reduct, _str, strlen(_str), REDUCT_ATOM_LOOKUP_NONE))

/**
 * @brief Create an atom handle from a number.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _val The number value.
 */
#define REDUCT_HANDLE_CREATE_NUMBER(_reduct, _val) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_number(_reduct, _val))

/**
 * @brief Create an atom handle from a native function.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _fn The native function pointer.
 */
#define REDUCT_HANDLE_CREATE_NATIVE(_reduct, _fn) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_native(_reduct, _fn))

/**
 * @brief Create a future handle.
 * 
 * @param _reduct Pointer to the Reduct structure.
 * @param _callable The callable handle.
 * @param _argc The number of arguments.
 * @param _argv Pointer to the arguments array.
 */
#define REDUCT_HANDLE_CREATE_FUTURE(_reduct, _callable, _argc, _argv) \
    REDUCT_HANDLE_FROM_FUTURE(reduct_future_new(_reduct, _callable, _argc, _argv))

/**
 * @brief Macro for iterating over all elements in a list handle.
 *
 * @param _handle The reduct_handle_t variable to store each element.
 * @param _list Pointer to the list handle.
 */
#define REDUCT_HANDLE_FOR_EACH(_handle, _list) \
    REDUCT_LIST_OF_EACH(_handle, REDUCT_HANDLE_TO_LIST(_list))

/**
 * @brief Get the value of the future referenced by the handle or the handle itself.
 * 
 * @param _reduct Pointer to the Reduct structure.
 * @param _handle The handle.
 * @return The result of the future if the handle is a future, otherwise the handle itself.
 */
#define REDUCT_HANDLE_JOIN(_reduct, _handle) \
    (REDUCT_HANDLE_IS_FUTURE(_handle) ? reduct_future_join(_reduct, REDUCT_HANDLE_TO_FUTURE(_handle)) : (_handle))

/**
 * @brief Get the constant nil handle.
 *
 * @param _reduct Pointer to the Reduct structure.
 */
#define REDUCT_HANDLE_NIL(_reduct) ((_reduct)->nil)

#define REDUCT_HANDLE_FALSE(_reduct) REDUCT_HANDLE_NIL(_reduct) ///< Constant false handle.

#define REDUCT_HANDLE_TRUE() REDUCT_HANDLE_FROM_NUMBER(1.0) ///< Constant true handle.

#define REDUCT_HANDLE_PI() REDUCT_HANDLE_FROM_NUMBER(REDUCT_PI) ///< Constant pi handle.

#define REDUCT_HANDLE_E() REDUCT_HANDLE_FROM_NUMBER(REDUCT_E) ///< Constant e handle.

#define REDUCT_HANDLE_INF() REDUCT_HANDLE_FROM_NUMBER(REDUCT_INF) ///< Constant infinity handle.

#define REDUCT_HANDLE_NAN() REDUCT_HANDLE_FROM_NUMBER(REDUCT_NAN) ///< Constant not a number handle.

/**
 * @brief Compare two handles using a given operator with a fast path for numbers.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The first handle.
 * @param _b The second handle.
 * @param _op The comparison operator (e.g., <, >, <=, >=, etc.).
 * @return The result of the comparison.
 */
#define REDUCT_HANDLE_COMPARE_FAST(_reduct, _a, _b, _op) \
    (REDUCT_LIKELY((_a)._value >= REDUCT_HANDLE_OFFSET_NUMBER && (_b)._value >= REDUCT_HANDLE_OFFSET_NUMBER) \
            ? (REDUCT_HANDLE_TO_NUMBER(_a) _op REDUCT_HANDLE_TO_NUMBER(_b)) \
            : (reduct_handle_compare(_reduct, (_a), (_b)) _op 0))

/**
 * @brief Perform a arithmetic operation on two handles with a fast path for numbers.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle.
 * @param _op The arithmetic operator, (e.g., +, -, *, etc.)
 */
#define REDUCT_HANDLE_ARITHMETIC_FAST(_reduct, _a, _b, _c, _op) \
    do \
    { \
        reduct_handle_t _bVal = (_b); \
        reduct_handle_t _cVal = (_c); \
        double _bv, _cv; \
        if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(_bVal) && REDUCT_HANDLE_IS_NUMBER(_cVal))) \
        { \
            _bv = REDUCT_HANDLE_TO_NUMBER(_bVal); \
            _cv = REDUCT_HANDLE_TO_NUMBER(_cVal); \
        } \
        else \
        { \
            _bv = reduct_handle_as_number(_reduct, _bVal); \
            _cv = reduct_handle_as_number(_reduct, _cVal); \
        } \
        *(_a) = REDUCT_HANDLE_FROM_NUMBER(_bv _op _cv); \
    } while (0)

/**
 * @brief Perform a division operation on two handles with a fast path for numbers.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle.
 */
#define REDUCT_HANDLE_DIV_FAST(_reduct, _a, _b, _c) \
    do \
    { \
        reduct_handle_t _bVal = (_b); \
        reduct_handle_t _cVal = (_c); \
        double _bv, _cv; \
        if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(_bVal) && REDUCT_HANDLE_IS_NUMBER(_cVal))) \
        { \
            _bv = REDUCT_HANDLE_TO_NUMBER(_bVal); \
            _cv = REDUCT_HANDLE_TO_NUMBER(_cVal); \
        } \
        else \
        { \
            _bv = reduct_handle_as_number(_reduct, _bVal); \
            _cv = reduct_handle_as_number(_reduct, _cVal); \
        } \
        if (REDUCT_UNLIKELY(_cv == 0.0)) \
        { \
            REDUCT_ERROR_THROW(_reduct, "division by zero"); \
        } \
        *(_a) = REDUCT_HANDLE_FROM_NUMBER(_bv / _cv); \
    } while (0)

/**
 * @brief Perform a modulo operation on two handles.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle.
 */
#define REDUCT_HANDLE_MOD_FAST(_reduct, _a, _b, _c) \
    do \
    { \
        reduct_handle_t _bVal = (_b); \
        reduct_handle_t _cVal = (_c); \
        double _bv, _cv; \
        if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(_bVal) && REDUCT_HANDLE_IS_NUMBER(_cVal))) \
        { \
            _bv = REDUCT_HANDLE_TO_NUMBER(_bVal); \
            _cv = REDUCT_HANDLE_TO_NUMBER(_cVal); \
        } \
        else \
        { \
            _bv = reduct_handle_as_number(_reduct, _bVal); \
            _cv = reduct_handle_as_number(_reduct, _cVal); \
        } \
        if (REDUCT_UNLIKELY(_cv == 0.0)) \
        { \
            REDUCT_ERROR_THROW(_reduct, "division by zero"); \
        } \
        *(_a) = REDUCT_HANDLE_FROM_NUMBER(fmod(_bv, _cv)); \
    } while (0)

/**
 * @brief Perform a bitwise operation on two handles.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle
 * @param _op The bitwise operator, (e.g., &, |, ^, etc.)
 */
#define REDUCT_HANDLE_BITWISE_FAST(_reduct, _a, _b, _c, _op) \
    do \
    { \
        reduct_handle_t _bVal = (_b); \
        reduct_handle_t _cVal = (_c); \
        double _bv, _cv; \
        if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(_bVal) && REDUCT_HANDLE_IS_NUMBER(_cVal))) \
        { \
            _bv = REDUCT_HANDLE_TO_NUMBER(_bVal); \
            _cv = REDUCT_HANDLE_TO_NUMBER(_cVal); \
        } \
        else \
        { \
            _bv = reduct_handle_as_number(_reduct, _bVal); \
            _cv = reduct_handle_as_number(_reduct, _cVal); \
        } \
        *(_a) = REDUCT_HANDLE_FROM_NUMBER((double)((int64_t)_bv _op(int64_t)_cv)); \
    } while (0)

/**
 * @brief Check if a handle is truthy.
 *
 * @param _handle Pointer to the handle.
 * @return `true` if the handle is truthy, `false` otherwise.
 */
#define REDUCT_HANDLE_IS_TRUTHY(_handle) \
    (!REDUCT_HANDLE_IS_LIST(_handle) || REDUCT_HANDLE_TO_LIST(_handle)->length != 0)

/**
 * @brief Retain a handle, preventing its referenced item from being collected by the garbage collector.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_RETAIN(_reduct, _handle) \
    do \
    { \
        reduct_handle_t _h = (_handle); \
        if (REDUCT_HANDLE_IS_ITEM(_h)) \
        { \
            reduct_gc_retain((_reduct), REDUCT_HANDLE_TO_ITEM(_h)); \
        } \
    } while (0)

/**
 * @brief Release a handle, allowing its referenced item to be collected by the garbage collector.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_RELEASE(_reduct, _handle) \
    do \
    { \
        reduct_handle_t _h = (_handle); \
        if (REDUCT_HANDLE_IS_ITEM(_h)) \
        { \
            reduct_gc_release((_reduct), REDUCT_HANDLE_TO_ITEM(_h)); \
        } \
    } while (0)

/**
 * @brief Ensure that a handle is an item handle.
 *
 * If the handle is a number, it will be upgraded to an item handle by looking up a corresponding atom.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle to ensure.
 */
REDUCT_API void reduct_handle_ensure_item(struct reduct* reduct, reduct_handle_t* handle);

/**
 * @brief Ensure that a handle is an item and return the pointer.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The item pointer.
 */
static inline REDUCT_ALWAYS_INLINE struct reduct_item* reduct_handle_as_item(struct reduct* reduct,
    reduct_handle_t handle)
{
    if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ITEM(handle)))
    {
        reduct_handle_ensure_item(reduct, &handle);
    }
    return REDUCT_HANDLE_TO_ITEM(handle);
}

/**
 * @brief Retrieve the integer representation of the handle.
 *
 * Will cast number values to integers or retrieve number values stored within an atom referenced by the handle
 * if the handle is itself not a number.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The integer value.
 */
static inline REDUCT_ALWAYS_INLINE int64_t reduct_handle_as_int(struct reduct* reduct, reduct_handle_t handle)
{
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(handle)))
    {
        return (int64_t)REDUCT_HANDLE_TO_NUMBER(handle);
    }

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(handle), "expected atom");
    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
    return reduct_atom_as_int(reduct, atom);
}

/**
 * @brief Retrieve the number representation of the handle.
 *
 * Will retrieve number values stored within an atom referenced by the handle
 * if the handle is itself not a number.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The number value.
 */
static inline REDUCT_ALWAYS_INLINE double reduct_handle_as_number(struct reduct* reduct, reduct_handle_t handle)
{
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(handle)))
    {
        return REDUCT_HANDLE_TO_NUMBER(handle);
    }

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(handle), "expected atom");
    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
    return reduct_atom_as_number(reduct, atom);
}

/**
 * @brief Retrieve the atom pointer of the handle.
 *
 * Will upgrade the handle to an item handle if it is a number.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The atom pointer.
 */
static inline REDUCT_ALWAYS_INLINE reduct_atom_t* reduct_handle_as_atom(struct reduct* reduct, reduct_handle_t handle)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_ATOM, "expected atom");
    return &item->atom;
}

/**
 * @brief Check if two items are exactly equal string-wise or structurally.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a The first handle, will be upgraded.
 * @param b The second handle, will be upgraded.
 * @return `true` if the items are strictly equal, `false` otherwise.
 */
REDUCT_API bool reduct_handle_is_equal(struct reduct* reduct, reduct_handle_t a, reduct_handle_t b);

/**
 * @brief Compare two items for ordering (less than, equal, or greater than).
 *
 * Useful for sorting or range checks.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a The first handle.
 * @param b The second handle.
 * @return A negative value if a < b, zero if a == b, and a positive value if a > b.
 */
REDUCT_API int64_t reduct_handle_compare(struct reduct* reduct, reduct_handle_t a, reduct_handle_t b);

/**
 * @brief Get the string pointer and length from an atom handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle to the atom.
 * @param outStr Pointer to store the string pointer, not `NULL` terminated.
 * @param outLen Pointer to store the string length.
 */
REDUCT_API void reduct_handle_atom_string(struct reduct* reduct, reduct_handle_t* handle, const char** outStr,
    size_t* outLen);

/**
 * @brief Push a value to a list handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list The list handle.
 * @param val The value handle to push.
 */
REDUCT_API void reduct_handle_push(struct reduct* reduct, reduct_handle_t list, reduct_handle_t val);

/**
 * @brief Get the element at the specified index from a list or atom handle.
 *
 * For lists, returns the nth element. For atoms, returns the nth character as a string handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @param index The index.
 * @return The element handle.
 */
REDUCT_API reduct_handle_t reduct_handle_at(struct reduct* reduct, reduct_handle_t handle, size_t index);

/**
 * @brief Get the length of a handle (list elements or atom characters).
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The length.
 */
REDUCT_API size_t reduct_handle_len(struct reduct* reduct, reduct_handle_t handle);

/**
 * @brief Check if an atom handle is equal to a string.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The atom handle.
 * @param str The string to compare.
 * @return `true` if the atom is equal to the string, `false` otherwise.
 */
REDUCT_API bool reduct_handle_is_str(struct reduct* reduct, reduct_handle_t handle, const char* str);

/** @} */

#endif
