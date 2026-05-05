#ifndef REDUCT_ATOM_H
#define REDUCT_ATOM_H 1

#include "reduct/defs.h"
#include "reduct/intrinsic.h"
#include "reduct/native.h"

struct reduct;

/**
 * @file atom.h
 * @brief Atom representation and operations.
 * @defgroup atom Atoms
 *
 * Atoms represent all strings within a Reduct expression, as such it also represents anything that a string can be,
 * including integers, floats and natives.
 * 
 * ## Interning
 * 
 * Some atoms, primarily atoms loaded during initial parsing, are "interned", meaning they are stored in a global map to ensure that only one instance of a given string exists at any time. This improves memory usage but primarily it allows us to cache if an atom represents a number or a native function, avoiding repeated parsing or lookups during execution.
 *
 * @see [Wikipedia String interning](https://en.wikipedia.org/wiki/String_interning)
 * 
 * ## Small and Large Strings
 * 
 * For small strings, of a length less than `REDUCT_ATOM_SMALL_MAX`, the string data is stored directly within the atom structure. 
 * 
 * For larger strings, the data is allocated from a dedicated atom stack, with the atom referencing this stack to prevent the garbage collector from collecting it.
 * 
 * ### Substrings and Superstrings
 * 
 * The stack system also allows multiple atoms to share the same buffer within a stack. 
 * 
 * For example, if we wish to create an atom containing a substring of a large atom, we can simply point to the middle of the existing buffer and reference the same stack.
 * 
 * Or, if we wish to create an atom that uses another atom as a prefix, and that other atom happens to be at the end of its stack, we can simply extend the allocation in place and return a new atom pointing to the same buffer.
 * 
 * @{
 */

#define REDUCT_ATOM_MAP_INITIAL 64 ///< The initial size of the atom map.
#define REDUCT_ATOM_MAP_GROWTH 2    ///< The growth factor of the atom map.
#define REDUCT_ATOM_SMALL_MAX 16 ///< The maximum length of a small atom.

#define REDUCT_ATOM_TOMBSTONE ((reduct_atom_t*)(uintptr_t)1) ///< Tombstone value for the atom map.

#define REDUCT_ATOM_INDEX_NONE ((reduct_uint32_t)-1) ///< The value of an unindexed atom.

/**
 * @brief Atom lookup flags.
 */
typedef enum
{
    REDUCT_ATOM_LOOKUP_NONE = 0,       ///< No flags.
    REDUCT_ATOM_LOOKUP_QUOTED = 1 << 0 ///< Atom should be explicitly quoted.
} reduct_atom_lookup_flags_t;

typedef reduct_uint8_t reduct_atom_flags_t;
#define REDUCT_ATOM_FLAG_NONE 0                ///< No flags.
#define REDUCT_ATOM_FLAG_INTEGER (1 << 0)   ///< Atom is known to be integer shaped.
#define REDUCT_ATOM_FLAG_FLOAT (1 << 1) ///< Atom is known to be float shaped.
#define REDUCT_ATOM_FLAG_INTRINSIC (1 << 2)    ///< Atom is known to represent an intrinsic.
#define REDUCT_ATOM_FLAG_NATIVE (1 << 3)       ///< Atom is known to represent a native function.
#define REDUCT_ATOM_FLAG_NUMBER_CHECKED (1 << 4) ///< Atom has been checked for integer/float shaping.
#define REDUCT_ATOM_FLAG_NATIVE_CHECKED (1 << 5) ///< Atom has been checked for a native function.
#define REDUCT_ATOM_FLAG_LARGE (1 << 6) ///< Atom has an allocated buffer within a stack.
#define REDUCT_ATOM_FLAG_OVERFLOW (1 << 7) ///< Atom has been parsed into an integer that is to large to fit into a `reduct_handle_t`.

#define REDUCT_ATOM_STACK_MIN 1024 ///< The minimum size of an atom stack.
#define REDUCT_ATOM_STACK_GROWTH 2 ///< The factor by which we increase the minimum size until the needed capacity is reached.

/**
 * @brief Atom block structure.
 * @struct reduct_atom_block_t
 * 
 * Used to more efficiently allocate large strings for atoms.
 */
typedef struct reduct_atom_stack
{
    struct reduct_atom_stack* next;
    struct reduct_atom_stack* prev;
    reduct_uint32_t capacity;
    reduct_uint32_t count;
    char* data;
} reduct_atom_stack_t;

/**
 * @brief Atom structure.
 * @struct reduct_atom_t
 */
typedef struct reduct_atom
{
    reduct_uint32_t length; ///< The length of the string (must be first, check the `reduct_item_t` structure).
    reduct_uint32_t hash;   ///< The hash of the string.
    reduct_uint32_t index; ///< The index within the atom map.
    reduct_atom_flags_t flags; ///< Atom flags.
    reduct_uint8_t _padding[3];
    char* string; ///< Pointer to the data.
    union
    {
        char smallString[REDUCT_ATOM_SMALL_MAX]; ///< Small string data, atom must not have `REDUCT_ATOM_FLAG_LARGE`.
        struct reduct_atom_stack* stack; ///< The stack that this atoms string was allocated from, atom must have `REDUCT_ATOM_FLAG_LARGE`.
    };
    union {
        reduct_int64_t integerValue; ///< Pre-computed integer value, atom must have `REDUCT_ATOM_FLAG_INTEGER`.
        reduct_float_t floatValue;   ///< Pre-computed float value, atom must have `REDUCT_ATOM_FLAG_FLOAT`.
        struct
        {
            reduct_native_fn native;     ///< Cached native function, atom must have `REDUCT_ATOM_FLAG_NATIVE`.
            reduct_native_intrinsic_fn intrinsic; ///< Cached intrinsic function, atom must have `REDUCT_ATOM_FLAG_NATIVE`.
        };
    };
} reduct_atom_t;

#define REDUCT_FNV_PRIME 16777619U    ///< FNV-1a 32-bit prime.
#define REDUCT_FNV_OFFSET 2166136261U ///< FNV-1a 32-bit offset basis.

/**
 * @brief Check if an atom is equal to a string.
 *
 * @param atom Pointer to the atom.
 * @param str The string to compare.
 * @param len The length of the string.
 * @return `REDUCT_TRUE` if the atom is equal to the string, `REDUCT_FALSE` otherwise.
 */
REDUCT_API reduct_bool_t reduct_atom_is_equal(reduct_atom_t* atom, const char* str,
    reduct_size_t len);

/**
 * @brief Check if two atoms are equal.
 *
 * @param a Pointer to the first atom.
 * @param b Pointer to the second atom.
 * @return `REDUCT_TRUE` if the atoms are equal, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_atom_is_equal_atom(reduct_atom_t* a, reduct_atom_t* b)
{
    if (a == b)
    {
        return REDUCT_TRUE;
    }
    if (a->length != b->length)
    {
        return REDUCT_FALSE;
    }
    return REDUCT_MEMCMP(a->string, b->string, a->length) == 0;
}

/**
 * @brief Create an atom with a reserved size.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param data The raw buffer to create the atom from.
 * @param len The length of the buffer.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new(struct reduct* reduct, reduct_size_t len);

/**
 * @brief Create an atom from a null-terminated string.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param str The null-terminated string.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new_string(struct reduct* reduct, const char* str);

/**
 * @brief Create an atom from a integer value.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param value The integer value.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new_int(struct reduct* reduct, reduct_int64_t value);

/**
 * @brief Create an atom from a float value.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param value The float value.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new_float(struct reduct* reduct, reduct_float_t value);

/**
 * @brief Create an atom for a anonymous native function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param native The native function pointer.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new_native(struct reduct* reduct, reduct_native_fn native);

/**
 * @brief Intern an existing atom into the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom to intern.
 */
REDUCT_API void reduct_atom_intern(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Lookup an atom in the Reduct structure.
 *
 * Will create a new atom if it does not exist.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param str The string to lookup.
 * @param len The length of the string.
 * @param flags Lookup flags to alter the interning behavior.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_lookup(struct reduct* reduct, const char* str, reduct_size_t len,
    reduct_atom_lookup_flags_t flags);

/**
 * @brief Cache if an atom is a number.
 *
 * @param atom Pointer to the atom.
 */
REDUCT_API void reduct_atom_check_number(reduct_atom_t* atom);

/**
 * @brief Cache if an atom is a native function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 */
REDUCT_API void reduct_atom_check_native(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Check if an atom is an intrinsic.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 * @return `REDUCT_TRUE` if the atom is an intrinsic, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_atom_is_intrinsic(struct reduct* reduct, reduct_atom_t* atom)
{
    if (REDUCT_UNLIKELY(!(atom->flags & REDUCT_ATOM_FLAG_NATIVE_CHECKED)))
    {
        reduct_atom_check_native(reduct, atom);
    }
    return (atom->flags & REDUCT_ATOM_FLAG_INTRINSIC) != 0;
}

/**
 * @brief Check if an atom is a native function.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 * @return `REDUCT_TRUE` if the atom is a native function, `REDUCT_FALSE` otherwise.
 */
static inline reduct_bool_t reduct_atom_is_native(struct reduct* reduct, reduct_atom_t* atom)
{
    if (REDUCT_UNLIKELY(!(atom->flags & REDUCT_ATOM_FLAG_NATIVE_CHECKED)))
    {
        reduct_atom_check_native(reduct, atom);
    }
    return (atom->flags & REDUCT_ATOM_FLAG_NATIVE) != 0;
}

/**
 * @brief Check if an atom is integer-shaped.
 *
 * @param atom Pointer to the atom.
 * @return `REDUCT_TRUE` if the atom is an integer, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_atom_is_int(reduct_atom_t* atom)
{
    if (REDUCT_UNLIKELY(!(atom->flags & REDUCT_ATOM_FLAG_NUMBER_CHECKED)))
    {
        reduct_atom_check_number(atom);
    }
    return (atom->flags & REDUCT_ATOM_FLAG_INTEGER) != 0;
}

/**
 * @brief Check if an atom is float-shaped.
 *
 * @param atom Pointer to the atom.
 * @return `REDUCT_TRUE` if the atom is a float, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_atom_is_float(reduct_atom_t* atom)
{
    if (REDUCT_UNLIKELY(!(atom->flags & REDUCT_ATOM_FLAG_NUMBER_CHECKED)))
    {
        reduct_atom_check_number(atom);
    }
    return (atom->flags & REDUCT_ATOM_FLAG_FLOAT) != 0;
}

/**
 * @brief Check if an atom is integer-shaped or float-shaped.
 *
 * @param atom Pointer to the atom.
 * @return `REDUCT_TRUE` if the atom is a number, `REDUCT_FALSE` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_atom_is_number(reduct_atom_t* atom)
{
    if (REDUCT_UNLIKELY(!(atom->flags & REDUCT_ATOM_FLAG_NUMBER_CHECKED)))
    {
        reduct_atom_check_number(atom);
    }
    return (atom->flags & (REDUCT_ATOM_FLAG_INTEGER | REDUCT_ATOM_FLAG_FLOAT)) != 0;
}

/**
 * @brief Get the integer value of an atom.
 *
 * @param atom Pointer to the atom.
 * @return The integer value.
 */
static inline REDUCT_ALWAYS_INLINE reduct_int64_t reduct_atom_get_int(reduct_atom_t* atom)
{
    REDUCT_ASSERT(reduct_atom_is_number(atom));

    if (reduct_atom_is_int(atom))
    {
        return atom->integerValue;
    }
    else
    {
        return (reduct_int64_t)atom->floatValue;
    }
}

/**
 * @brief Get the float value of an atom.
 *
 * @param atom Pointer to the atom.
 * @return The float value.
 */
static inline REDUCT_ALWAYS_INLINE reduct_float_t reduct_atom_get_float(reduct_atom_t* atom)
{
    REDUCT_ASSERT(reduct_atom_is_number(atom));

    if (reduct_atom_is_float(atom))
    {
        return atom->floatValue;
    }
    else
    {
        return (reduct_float_t)atom->integerValue;
    }
}

/**
 * @brief Create a substring of an existing atom.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the source atom.
 * @param start The starting index.
 * @param len The length of the substring.
 * @return A pointer to the new atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_substr(struct reduct* reduct, reduct_atom_t* atom, reduct_size_t start,
    reduct_size_t len);

/**
 * @brief Create a superstring of an existing atom.
 *
 * If the atom is at the end of its stack and there is enough capacity, it will extend the existing allocation. Otherwise, it will allocate a new atom and copy the existing data.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the source atom.
 * @param len The new total length.
 * @return A pointer to the new atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_superstr(struct reduct* reduct, reduct_atom_t* atom, reduct_size_t len);

/**
 * @brief Create a new atom by copying data directly into it.
 *
 * The atom is NOT interned and its hash is set to 0, avoiding
 * the overhead of hash computation and map lookup.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param data The data to copy.
 * @param len The length of the data.
 * @return A pointer to the new atom.
 */
static inline REDUCT_ALWAYS_INLINE reduct_atom_t* reduct_atom_new_copy(struct reduct* reduct, const char* data,
    reduct_size_t len)
{
    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    REDUCT_MEMCPY(atom->string, data, len);
    return atom;
}

/** @} */

#endif
