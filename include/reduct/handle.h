#ifndef REDUCT_HANDLE_H
#define REDUCT_HANDLE_H 1

struct reduct;

#include "reduct/defs.h"
#include "reduct/item.h"

/**
 * @file handle.h
 * @brief Handle management.
 * @defgroup handle Handle
 *
 * A handle is a lightweight reference to a Reduct item, with the ability to cache various flags from its referenced
 * item or the integer/float value of an atom using Tagged Pointers (NaN Boxing).
 *
 * ## 64-bit Handle Bit Layout
 *
 * The top 16 bits are used as a type tag. The remaining 48 bits represent the payload
 * (either a 48-bit integer or a 48-bit pointer).
 *
 * | Tag (16 bits)   | Payload (48 bits)                 |
 * |-----------------|-----------------------------------|
 * | `0x0000`        | Item Pointer (`reduct_item_t*`)     |
 * | `0x0006`        | Integer Value (48-bit signed)     |
 * | `0x0007...FFFF` | Float (Shifted IEEE 754 double)   |
 *
 * @see [Wikipedia Tagged pointer](https://en.wikipedia.org/wiki/Tagged_pointer)
 *
 * @{
 */

/**
 * @brief Invalid handle constant.
 */
#define REDUCT_HANDLE_NONE 0x0000000000000000ULL

#define REDUCT_HANDLE_OFFSET_FLOAT 0x0007000000000000ULL ///< Offset used for encoding doubles.

#define REDUCT_HANDLE_TAG_INT 0x0006000000000000ULL  ///< Tag for integer handles.
#define REDUCT_HANDLE_TAG_ITEM 0x0000000000000000ULL ///< Tag for item handles.

#define REDUCT_HANDLE_MASK_TAG 0xFFFF000000000000ULL   ///< Mask for handle tag bits.
#define REDUCT_HANDLE_MASK_VAL 0x0000FFFFFFFFFFFFULL   ///< Mask for handle value bits.
#define REDUCT_HANDLE_MASK_PTR REDUCT_HANDLE_MASK_VAL  ///< Mask for item pointer bits.
#define REDUCT_HANDLE_MASK_FLAGS 0x00000000000000FFULL ///< Mask for flags (not stored in handle anymore).

#define REDUCT_HANDLE_INT_MAX 0x00007FFFFFFFFFFFULL ///< Maximum value for a 48-bit signed integer in a handle.
#define REDUCT_HANDLE_INT_WIDTH 48 ///< The bit width of the integer payload in a handle.

/**
 * @brief Create a handle from an integer.
 *
 * @param _val The integer value.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_INT(_val) (REDUCT_HANDLE_TAG_INT | ((reduct_handle_t)(_val) & REDUCT_HANDLE_MASK_VAL))

/**
 * @brief Create a handle from a float.
 *
 * @param _val The float value.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_FLOAT(_val) \
    (((union { \
        double d; \
        reduct_handle_t u; \
    }){.d = (_val)}) \
            .u + \
        REDUCT_HANDLE_OFFSET_FLOAT)

/**
 * @brief Create a handle from an item pointer.
 *
 * @param _ptr The pointer to the reduct_item_t.
 * @return The handle.
 */
#define REDUCT_HANDLE_FROM_ITEM(_ptr) \
    (REDUCT_HANDLE_TAG_ITEM | ((reduct_handle_t)(void*)(_ptr) & REDUCT_HANDLE_MASK_PTR))

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
 * @brief Check if a handle is an integer.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an integer, zero otherwise.
 */
#define REDUCT_HANDLE_IS_INT(_handle) (((*(_handle)) & REDUCT_HANDLE_MASK_TAG) == REDUCT_HANDLE_TAG_INT)

/**
 * @brief Check if a handle is a float.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a float, zero otherwise.
 */
#define REDUCT_HANDLE_IS_FLOAT(_handle) ((*(_handle)) >= REDUCT_HANDLE_OFFSET_FLOAT)

/**
 * @brief Check if a handle is an integer of references a integer shaped item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is integer shaped, zero otherwise.
 */
#define REDUCT_HANDLE_IS_INT_SHAPED(_handle) \
    (REDUCT_HANDLE_IS_INT(_handle) || \
     (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_IS_ATOM(_handle) && \
      reduct_atom_is_int(REDUCT_HANDLE_TO_ATOM(_handle))))

/**
 * @brief Check if a handle is a float or references a float shaped item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is float shaped, zero otherwise.
 */
#define REDUCT_HANDLE_IS_FLOAT_SHAPED(_handle) \
    (REDUCT_HANDLE_IS_FLOAT(_handle) || \
     (REDUCT_HANDLE_IS_ITEM(_handle) && REDUCT_HANDLE_IS_ATOM(_handle) && \
      reduct_atom_is_float(REDUCT_HANDLE_TO_ATOM(_handle))))

/**
 * @brief Check if a handle is a number or references a number shaped item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a number, zero otherwise.
 */
#define REDUCT_HANDLE_IS_NUMBER_SHAPED(_handle) \
    (REDUCT_HANDLE_IS_INT_SHAPED(_handle) || REDUCT_HANDLE_IS_FLOAT_SHAPED(_handle))

/**
 * @brief Get the type of the item referenced by the handle, or `REDUCT_ITEM_TYPE_ATOM` if not an item.
 *
 * @param _handle Pointer to the handle.
 */
#define REDUCT_HANDLE_GET_TYPE(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) ? REDUCT_HANDLE_TO_ITEM(_handle)->type : REDUCT_ITEM_TYPE_ATOM)

/**
 * @brief Get the string representation of the type of the item referenced by the handle.
 *
 * @param _handle Pointer to the handle.
 * @return The string representation of the item type.
 */
#define REDUCT_HANDLE_GET_TYPE_STR(_handle) \
    (REDUCT_HANDLE_IS_ITEM(_handle) ? reduct_item_type_str(REDUCT_HANDLE_TO_ITEM(_handle)) : (REDUCT_HANDLE_IS_INT(_handle) ? "int" : "float"))

/**
 * @brief Check if a handle is an item.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an item, zero otherwise.
 */
#define REDUCT_HANDLE_IS_ITEM(_handle) (((*(_handle)) & REDUCT_HANDLE_MASK_TAG) == REDUCT_HANDLE_TAG_ITEM)

/**
 * @brief Check if a handle is an atom.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is an atom, zero otherwise.
 */
#define REDUCT_HANDLE_IS_ATOM(_handle) (REDUCT_HANDLE_GET_TYPE(_handle) == REDUCT_ITEM_TYPE_ATOM)

/**
 * @brief Check if a handle is a list.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a list, zero otherwise.
 */
#define REDUCT_HANDLE_IS_LIST(_handle) (REDUCT_HANDLE_GET_TYPE(_handle) == REDUCT_ITEM_TYPE_LIST)

/**
 * @brief Check if a handle is a function.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a function, zero otherwise.
 */
#define REDUCT_HANDLE_IS_FUNCTION(_handle) (REDUCT_HANDLE_GET_TYPE(_handle) == REDUCT_ITEM_TYPE_FUNCTION)

/**
 * @brief Check if a handle is a closure.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is a closure, zero otherwise.
 */
#define REDUCT_HANDLE_IS_CLOSURE(_handle) (REDUCT_HANDLE_GET_TYPE(_handle) == REDUCT_ITEM_TYPE_CLOSURE)

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
 * @brief Check if a handle is callable.
 *
 * @param _handle Pointer to the handle.
 * @return Non-zero if the handle is callable, zero otherwise.
 */
#define REDUCT_HANDLE_IS_CALLABLE(_reduct, _handle) (REDUCT_HANDLE_IS_LAMBDA(_handle) || REDUCT_HANDLE_IS_NATIVE(_reduct, _handle))

/**
 * @brief Get the integer value of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The integer value.
 */
#define REDUCT_HANDLE_TO_INT(_handle) (((reduct_int64_t)((*(_handle)) << 16)) >> 16)

/**
 * @brief Get the float value of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The float value.
 */
#define REDUCT_HANDLE_TO_FLOAT(_handle) \
    (((union { \
        reduct_handle_t u; \
        double d; \
    }){.u = (*(_handle)) - REDUCT_HANDLE_OFFSET_FLOAT}) \
            .d)

/**
 * @brief Get the item pointer of a handle.
 *
 * @param _handle Pointer to the handle.
 * @return The item pointer.
 */
#define REDUCT_HANDLE_TO_ITEM(_handle) ((reduct_item_t*)(void*)((*(_handle)) & REDUCT_HANDLE_MASK_PTR))

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
 * @brief Create a list handle.
 *
 * @param _reduct Pointer to the Reduct structure.
 */
#define REDUCT_HANDLE_LIST(_reduct) REDUCT_HANDLE_FROM_LIST(reduct_list_new(_reduct))

/**
 * @brief Create a list handle from an array of handles.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _count The number of handles.
 * @param _handles The array of handles.
 */
#define REDUCT_HANDLE_HANDLES(_reduct, _count, _handles) REDUCT_HANDLE_FROM_LIST(reduct_list_new_handles(_reduct, _count, _handles))

/**
 * @brief Create a list handle of pairs (key-value) from a variable number of pairs.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _count The number of pairs.
 * @param ... Each pair should be provided as a `(const char*, reduct_handle_t)`.
 */
#define REDUCT_HANDLE_PAIRS(_reduct, _count, ...) REDUCT_HANDLE_FROM_LIST(reduct_list_new_pairs(_reduct, _count, __VA_ARGS__))

/**
 * @brief Create an atom handle with a reserved size.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _len The length of the buffer.
 */
#define REDUCT_HANDLE_ATOM(_reduct, _len) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(_reduct, _len))

/**
 * @brief Create an atom handle from a string.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _str The null-terminated string.
 */
#define REDUCT_HANDLE_STRING(_reduct, _str) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_string(_reduct, _str))

/**
 * @brief Create an interned atom handle from a string.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _str The null-terminated string.
 */
#define REDUCT_HANDLE_SYM(_reduct, _str) \
    REDUCT_HANDLE_FROM_ATOM(reduct_atom_lookup(_reduct, _str, REDUCT_STRLEN(_str), REDUCT_ATOM_LOOKUP_NONE))

/**
 * @brief Create an atom handle from an integer.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _val The integer value.
 */
#define REDUCT_HANDLE_INT(_reduct, _val) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_int(_reduct, _val))

/**
 * @brief Create an atom handle from a float.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _val The float value.
 */
#define REDUCT_HANDLE_FLOAT(_reduct, _val) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_float(_reduct, _val))

/**
 * @brief Create an atom handle from a native function.
 */
#define REDUCT_HANDLE_NATIVE(_reduct, _fn) REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_native(_reduct, _fn))

/**
 * @brief Macro for iterating over all elements in a list handle.
 *
 * @param _handle The reduct_handle_t variable to store each element.
 * @param _list Pointer to the list handle.
 */
#define REDUCT_HANDLE_LIST_FOR_EACH(_handle, _list) \
    for (reduct_list_iter_t _iter = REDUCT_LIST_ITER(&REDUCT_HANDLE_TO_ITEM(_list)->list); \
         reduct_list_iter_next(&_iter, (_handle));)

/**
 * @brief Get flags from an item handle.
 *
 * @param _handle Pointer to the handle.
 * @return The flags stored in the handle.
 */
#define REDUCT_HANDLE_GET_FLAGS(_handle) (REDUCT_HANDLE_IS_ITEM(_handle) ? REDUCT_HANDLE_TO_ITEM(_handle)->flags : 0)

#define REDUCT_HANDLE_FALSE() REDUCT_HANDLE_FROM_INT(0) ///< Constant false handle.

#define REDUCT_HANDLE_TRUE() REDUCT_HANDLE_FROM_INT(1) ///< Constant true handle.

/**
 * @brief Create a boolean handle from a C condition.
 *
 * @param _cond The condition to evaluate.
 */
#define REDUCT_HANDLE_FROM_BOOL(_cond) REDUCT_HANDLE_FROM_INT(!!(_cond))

/**
 * @brief Compare two handles using a given operator with a fast path for integers and floats.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The first handle.
 * @param _b The second handle.
 * @param _op The comparison operator (e.g., <, >, <=, >=, etc.).
 * @return The result of the comparison.
 */
#define REDUCT_HANDLE_COMPARE_FAST(_reduct, _a, _b, _op) \
    (REDUCT_LIKELY(((((*(_a)) ^ REDUCT_HANDLE_TAG_INT) | ((*(_b)) ^ REDUCT_HANDLE_TAG_INT)) & REDUCT_HANDLE_MASK_TAG) == 0) \
            ? (REDUCT_HANDLE_TO_INT(_a) _op REDUCT_HANDLE_TO_INT(_b)) \
            : (((*(_a)) >= REDUCT_HANDLE_OFFSET_FLOAT && (*(_b)) >= REDUCT_HANDLE_OFFSET_FLOAT) \
                      ? (REDUCT_HANDLE_TO_FLOAT(_a) _op REDUCT_HANDLE_TO_FLOAT(_b)) \
                      : (reduct_handle_compare(_reduct, _a, _b) _op 0)))

/**
 * @brief Perform a arithmetic operation on two handles with a fast path for integers and floats.
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
        reduct_handle_t _bVal = *(_b); \
        reduct_handle_t _cVal = *(_c); \
        if (REDUCT_LIKELY( \
                (((_bVal ^ REDUCT_HANDLE_TAG_INT) | (_cVal ^ REDUCT_HANDLE_TAG_INT)) & REDUCT_HANDLE_MASK_TAG) == 0)) \
        { \
            *(_a) = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&_bVal) _op REDUCT_HANDLE_TO_INT(&_cVal)); \
        } \
        else if (REDUCT_LIKELY(REDUCT_HANDLE_IS_FLOAT(&_bVal) && REDUCT_HANDLE_IS_FLOAT(&_cVal))) \
        { \
            *(_a) = REDUCT_HANDLE_FROM_FLOAT(REDUCT_HANDLE_TO_FLOAT(&_bVal) _op REDUCT_HANDLE_TO_FLOAT(&_cVal)); \
        } \
        else \
        { \
            reduct_promotion_t prom; \
            reduct_handle_promote(_reduct, _b, _c, &prom); \
            if (prom.type == REDUCT_PROMOTION_TYPE_INT) \
            { \
                *(_a) = REDUCT_HANDLE_FROM_INT(prom.a.intVal _op prom.b.intVal); \
            } \
            else \
            { \
                *(_a) = REDUCT_HANDLE_FROM_FLOAT(prom.a.floatVal _op prom.b.floatVal); \
            } \
        } \
    } while (0)

/**
 * @brief Perform a division operation on two handles with a fast path for integers and floats.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle.
 */
#define REDUCT_HANDLE_DIV_FAST(_reduct, _a, _b, _c) \
    do \
    { \
        reduct_handle_t _bVal = *(_b); \
        reduct_handle_t _cVal = *(_c); \
        if (REDUCT_LIKELY( \
                (((_bVal ^ REDUCT_HANDLE_TAG_INT) | (_cVal ^ REDUCT_HANDLE_TAG_INT)) & REDUCT_HANDLE_MASK_TAG) == 0)) \
        { \
            reduct_int64_t _cv = REDUCT_HANDLE_TO_INT(&_cVal); \
            if (REDUCT_UNLIKELY(_cv == 0)) \
            { \
                REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
            } \
            *(_a) = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&_bVal) / _cv); \
        } \
        else if (REDUCT_LIKELY(REDUCT_HANDLE_IS_FLOAT(&_bVal) && REDUCT_HANDLE_IS_FLOAT(&_cVal))) \
        { \
            double _cv = REDUCT_HANDLE_TO_FLOAT(&_cVal); \
            if (REDUCT_UNLIKELY(_cv == 0.0)) \
            { \
                REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
            } \
            *(_a) = REDUCT_HANDLE_FROM_FLOAT(REDUCT_HANDLE_TO_FLOAT(&_bVal) / _cv); \
        } \
        else \
        { \
            reduct_promotion_t prom; \
            reduct_handle_promote(_reduct, _b, _c, &prom); \
            if (prom.type == REDUCT_PROMOTION_TYPE_INT) \
            { \
                if (REDUCT_UNLIKELY(prom.b.intVal == 0)) \
                { \
                    REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
                } \
                *(_a) = REDUCT_HANDLE_FROM_INT(prom.a.intVal / prom.b.intVal); \
            } \
            else \
            { \
                if (REDUCT_UNLIKELY(prom.b.floatVal == 0.0)) \
                { \
                    REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
                } \
                *(_a) = REDUCT_HANDLE_FROM_FLOAT(prom.a.floatVal / prom.b.floatVal); \
            } \
        } \
    } while (0)

/**
 * @brief Perform a modulo operation on two handles with a fast path for integers.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _a The target handle.
 * @param _b The first handle.
 * @param _c The second handle.
 */
#define REDUCT_HANDLE_MOD_FAST(_reduct, _a, _b, _c) \
    do \
    { \
        reduct_handle_t _bVal = *(_b); \
        reduct_handle_t _cVal = *(_c); \
        if (REDUCT_LIKELY( \
                (((_bVal ^ REDUCT_HANDLE_TAG_INT) | (_cVal ^ REDUCT_HANDLE_TAG_INT)) & REDUCT_HANDLE_MASK_TAG) == 0)) \
        { \
            reduct_int64_t _cv = REDUCT_HANDLE_TO_INT(&_cVal); \
            if (REDUCT_UNLIKELY(_cv == 0)) \
            { \
                REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
            } \
            *(_a) = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&_bVal) % _cv); \
        } \
        else \
        { \
            reduct_int64_t _bv = reduct_get_int(_reduct, &_bVal); \
            reduct_int64_t _cv = reduct_get_int(_reduct, &_cVal); \
            if (REDUCT_UNLIKELY(_cv == 0)) \
            { \
                REDUCT_ERROR_RUNTIME(_reduct, "division by zero"); \
            } \
            *(_a) = REDUCT_HANDLE_FROM_INT(_bv % _cv); \
        } \
    } while (0)

/**
 * @brief Perform a bitwise operation on two handles with a fast path for integers and floats.
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
        reduct_handle_t _bVal = *(_b); \
        reduct_handle_t _cVal = *(_c); \
        if (REDUCT_LIKELY( \
                (((_bVal ^ REDUCT_HANDLE_TAG_INT) | (_cVal ^ REDUCT_HANDLE_TAG_INT)) & REDUCT_HANDLE_MASK_TAG) == 0)) \
        { \
            *(_a) = REDUCT_HANDLE_FROM_INT(REDUCT_HANDLE_TO_INT(&_bVal) _op REDUCT_HANDLE_TO_INT(&_cVal)); \
        } \
        else \
        { \
            *(_a) = REDUCT_HANDLE_FROM_INT(reduct_get_int(_reduct, &_bVal) _op reduct_get_int(_reduct, &_cVal)); \
        } \
    } while (0)

/**
 * @brief Check if a handle is truthy.
 *
 * @param _handle Pointer to the handle.
 * @return `REDUCT_TRUE` if the handle is truthy, `REDUCT_FALSE` otherwise.
 */
#define REDUCT_HANDLE_IS_TRUTHY(_handle) \
    (REDUCT_HANDLE_IS_INT(_handle) \
            ? ((*(_handle) & REDUCT_HANDLE_MASK_VAL) != 0) \
            : (REDUCT_HANDLE_IS_FLOAT(_handle) \
                      ? (REDUCT_HANDLE_TO_FLOAT(_handle) != 0.0) \
                      : (REDUCT_HANDLE_IS_ITEM(_handle) \
                                ? ((*(_handle)) != REDUCT_HANDLE_NONE && \
                                      !(REDUCT_HANDLE_GET_FLAGS(_handle) & REDUCT_ITEM_FLAG_FALSY)) \
                                : REDUCT_FALSE)))

/**
 * @brief Retain a handle, preventing its referenced item from being collected by the garbage collector.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _handle Pointer to the handle.
 */                      
#define REDUCT_HANDLE_RETAIN(_reduct, _handle) \
    do \
    { \
        reduct_handle_t* _h = (_handle); \
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
        reduct_handle_t* _h = (_handle); \
        if (REDUCT_HANDLE_IS_ITEM(_h)) \
        { \
            reduct_gc_release((_reduct), REDUCT_HANDLE_TO_ITEM(_h)); \
        } \
    } while (0)

/**
 * @brief Ensure that a handle is an item handle.
 *
 * If the handle is an integer or float, it will be upgraded to an item handle by looking up a corresponding atom.
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
static inline REDUCT_ALWAYS_INLINE struct reduct_item* reduct_handle_as_item(struct reduct* reduct, reduct_handle_t* handle)
{
    if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ITEM(handle)))
    {
        reduct_handle_ensure_item(reduct, handle);
    }
    return REDUCT_HANDLE_TO_ITEM(handle);
}

/**
 * @brief Promotion types for numeric operations.
 * @enum reduct_promotion_type_t
 */
typedef enum
{
    REDUCT_PROMOTION_TYPE_NONE = 0,
    REDUCT_PROMOTION_TYPE_INT = 1,
    REDUCT_PROMOTION_TYPE_FLOAT = 2
} reduct_promotion_type_t;

/**
 * @brief Promotion result for numeric operations.
 * @struct reduct_promotion_t
 */
typedef struct
{
    reduct_promotion_type_t type;
    union {
        reduct_int64_t intVal;
        reduct_float_t floatVal;
    } a;
    union {
        reduct_int64_t intVal;
        reduct_float_t floatVal;
    } b;
} reduct_promotion_t;

/**
 * @brief Promote two handles to a common numeric type.
 *
 * If both handles are numeric, they are promoted to the highest precision type (float if either is a float, otherwise
 * integer).
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a The first handle.
 * @param b The second handle.
 * @param out The promotion result structure.
 */
REDUCT_API void reduct_handle_promote(struct reduct* reduct, reduct_handle_t* a, reduct_handle_t* b,
    reduct_promotion_t* out);

/**
 * @brief Check if two items are exactly equal string-wise or structurally.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a The first handle, will be upgraded.
 * @param b The second handle, will be upgraded.
 * @return `REDUCT_TRUE` if the items are strictly equal, `REDUCT_FALSE` otherwise.
 */
REDUCT_API reduct_bool_t reduct_handle_is_equal(struct reduct* reduct, reduct_handle_t* a, reduct_handle_t* b);

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
REDUCT_API reduct_int64_t reduct_handle_compare(struct reduct* reduct, reduct_handle_t* a, reduct_handle_t* b);

/**
 * @brief Compare two handles that are most likely atoms.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a The first handle.
 * @param b The second handle.
 * @return `REDUCT_TRUE` if the handles are equal, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_handle_compare_likely_atom(struct reduct* reduct, reduct_handle_t* a, reduct_handle_t* b)
{
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_ATOM(a) && REDUCT_HANDLE_IS_ATOM(b)))
    {
        if (reduct_atom_is_equal_atom(REDUCT_HANDLE_TO_ATOM(a), REDUCT_HANDLE_TO_ATOM(b)))
        {
            return REDUCT_TRUE;
        }
    } 
    else 
    {
        if (reduct_handle_compare(reduct, a, b) == 0)
        {
            return REDUCT_TRUE;
        }
    }

    return REDUCT_FALSE;
}

/**
 * @brief Get the constant nil handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The nil handle.
 */
REDUCT_API reduct_handle_t reduct_handle_nil(struct reduct* reduct);

/**
 * @brief Get the constant PI handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The PI handle.
 */
REDUCT_API reduct_handle_t reduct_handle_pi(struct reduct* reduct);

/**
 * @brief Get the constant E handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The E handle.
 */
REDUCT_API reduct_handle_t reduct_handle_e(struct reduct* reduct);

/**
 * @brief Get the string pointer and length from an atom handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle to the atom.
 * @param outStr Pointer to store the string pointer, not `NULL` terminated.
 * @param outLen Pointer to store the string length.
 */
REDUCT_API void reduct_handle_atom_string(struct reduct* reduct, reduct_handle_t* handle, const char** outStr,
    reduct_size_t* outLen);

/**
 * @brief Push a value to a list handle.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list The list handle.
 * @param val The value handle to push.
 */
REDUCT_API void reduct_handle_push(struct reduct* reduct, reduct_handle_t* list, reduct_handle_t val);

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
REDUCT_API reduct_handle_t reduct_handle_at(struct reduct* reduct, reduct_handle_t* handle, reduct_size_t index);

/**
 * @brief Get the length of a handle (list elements or atom characters).
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle.
 * @return The length.
 */
REDUCT_API reduct_size_t reduct_handle_len(struct reduct* reduct, reduct_handle_t* handle);

/**
 * @brief Check if an atom handle is equal to a string.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The atom handle.
 * @param str The string to compare.
 * @return `REDUCT_TRUE` if the atom is equal to the string, `REDUCT_FALSE` otherwise.
 */
REDUCT_API reduct_bool_t reduct_handle_is_str(struct reduct* reduct, reduct_handle_t* handle, const char* str);

/** @} */

#endif
