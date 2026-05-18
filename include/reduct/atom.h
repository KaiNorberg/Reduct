#ifndef REDUCT_ATOM_H
#define REDUCT_ATOM_H 1

#include "reduct/defs.h"
#include "reduct/intrinsic.h"
#include "reduct/native.h"
#include "reduct/schema.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
 * Some atoms, primarily atoms loaded during initial parsing, are "interned", meaning they are stored in a global map to
 * ensure that only one instance of a given string exists at any time. This improves memory usage but primarily it
 * allows us to cache if an atom represents a number or a native function, avoiding repeated parsing or lookups during
 * execution.
 *
 * @see [Wikipedia String interning](https://en.wikipedia.org/wiki/String_interning)
 *
 * ## Small and Large Strings
 *
 * For small strings, of a length less than `REDUCT_ATOM_SMALL_MAX`, the string data is stored directly within the atom
 * structure.
 *
 * For larger strings, the data is allocated from a dedicated atom stack, with the atom referencing this stack to
 * prevent the garbage collector from collecting it.
 *
 * ### Substrings and Superstrings
 *
 * The stack system also allows multiple atoms to share the same buffer within a stack.
 *
 * For example, if we wish to create an atom containing a substring of a large atom, we can simply point to the middle
 * of the existing buffer and reference the same stack.
 *
 * Or, if we wish to create an atom that uses another atom as a prefix, and that other atom happens to be at the end of
 * its stack, we can simply extend the allocation in place and return a new atom pointing to the same buffer.
 *
 * @{
 */

#define REDUCT_ATOM_MAP_INITIAL 64 ///< The initial size of the atom map.
#define REDUCT_ATOM_MAP_GROWTH 2   ///< The growth factor of the atom map.
#define REDUCT_ATOM_SMALL_MAX 16   ///< The maximum length of a small atom.

#define REDUCT_ATOM_TOMBSTONE ((reduct_atom_t*)(uintptr_t)1) ///< Tombstone value for the atom map.

#define REDUCT_ATOM_INDEX_NONE ((uint32_t)-1) ///< The value of an unindexed atom.

/**
 * @brief Atom lookup flags.
 */
typedef enum
{
    REDUCT_ATOM_LOOKUP_NONE = 0,       ///< No flags.
    REDUCT_ATOM_LOOKUP_QUOTED = 1 << 0 ///< Atom should be explicitly quoted.
} reduct_atom_lookup_flags_t;

typedef uint16_t reduct_atom_flags_t;
#define REDUCT_ATOM_FLAG_NONE 0                  ///< No flags.
#define REDUCT_ATOM_FLAG_INTEGER (1 << 0)        ///< Atom is known to be integer shaped.
#define REDUCT_ATOM_FLAG_FLOAT (1 << 1)          ///< Atom is known to be float shaped.
#define REDUCT_ATOM_FLAG_INTRINSIC (1 << 2)      ///< Atom is known to represent an intrinsic.
#define REDUCT_ATOM_FLAG_NATIVE (1 << 3)         ///< Atom is known to represent a native function.
#define REDUCT_ATOM_FLAG_NUMBER_CHECKED (1 << 4) ///< Atom has been checked for integer/float shaping.
#define REDUCT_ATOM_FLAG_NATIVE_CHECKED (1 << 5) ///< Atom has been checked for a native function.
#define REDUCT_ATOM_FLAG_LARGE (1 << 6)          ///< Atom has an allocated buffer within a stack.
#define REDUCT_ATOM_FLAG_OVERFLOW \
    (1 << 7) ///< Atom has been parsed into an integer that is to large to fit into a `reduct_handle_t`.
#define REDUCT_ATOM_FLAG_SCHEMA (1 << 8) ///< Atom is a schema field.
#define REDUCT_ATOM_FLAG_QUOTED (1 << 9) ///< Atom is quoted.

#define REDUCT_ATOM_STACK_MIN 1024 ///< The minimum size of an atom stack.
#define REDUCT_ATOM_STACK_GROWTH \
    2 ///< The factor by which we increase the minimum size until the needed capacity is reached.

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
    uint32_t capacity;
    uint32_t count;
    char* data;
} reduct_atom_stack_t;

/**
 * @brief Atom structure.
 * @struct reduct_atom_t
 */
typedef struct reduct_atom
{
    uint32_t length;           ///< The length of the string (must be first, check the `reduct_item_t` structure).
    uint32_t hash;             ///< The hash of the string.
    uint32_t index;            ///< The index within the atom map.
    reduct_atom_flags_t flags; ///< Atom flags.
    uint8_t _padding[2];
    char* string; ///< Pointer to the data.
    union {
        char smallString[REDUCT_ATOM_SMALL_MAX]; ///< Small string data, atom must not have `REDUCT_ATOM_FLAG_LARGE`.
        struct reduct_atom_stack*
            stack; ///< The stack that this atoms string was allocated from, atom must have `REDUCT_ATOM_FLAG_LARGE`.
    };
    union {
        struct
        {
            /**
             * An array of indexes to which this atom is a key.
             *
             * The array is indexed by the schema id and stores the index of the field within the schema.
             */
            reduct_schema_index_t* schema;
            uint32_t schemaCount;
        };
        int64_t integerValue; ///< Pre-computed integer value, atom must have `REDUCT_ATOM_FLAG_INTEGER`.
        double floatValue;    ///< Pre-computed float value, atom must have `REDUCT_ATOM_FLAG_FLOAT`.
        struct
        {
            reduct_native_fn native; ///< Cached native function, atom must have `REDUCT_ATOM_FLAG_NATIVE`.
            reduct_native_intrinsic_fn
                intrinsic; ///< Cached intrinsic function, atom must have `REDUCT_ATOM_FLAG_NATIVE`.
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
 * @return `true` if the atom is equal to the string, `false` otherwise.
 */
REDUCT_API bool reduct_atom_is_equal(reduct_atom_t* atom, const char* str, size_t len);

/**
 * @brief Create an atom with a reserved size.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param data The raw buffer to create the atom from.
 * @param len The length of the buffer.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new(struct reduct* reduct, size_t len);

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
REDUCT_API reduct_atom_t* reduct_atom_new_int(struct reduct* reduct, int64_t value);

/**
 * @brief Create an atom from a float value.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param value The float value.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_new_float(struct reduct* reduct, double value);

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
 * @return `true` if the atom is already interned or it was successfully added, `false` if an identical atom is already
 * interned.
 */
REDUCT_API bool reduct_atom_intern(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Lookup an interned atom in the Reduct structure.
 *
 * Will create and intern a new atom if it does not exist.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param str The string to lookup.
 * @param len The length of the string.
 * @param flags Lookup flags to alter the interning behavior.
 * @return A pointer to the atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_lookup(struct reduct* reduct, const char* str, size_t len,
    reduct_atom_lookup_flags_t flags);

/**
 * @brief Retrieve the lookup flags required to lookup this specific atom.
 *
 * Really just used to check if an atom is quoted or not.
 *
 * @param atom The atom to check.
 * @return The lookup flags.
 */
REDUCT_API reduct_atom_lookup_flags_t reduct_atom_get_lookup_flags(reduct_atom_t* atom);

/**
 * @brief Ensure an atom is interned.
 *
 * If the atom is already interned, it returns the existing atom.
 * If an identical atom is already interned, the already interned atom is returned.
 * Otherwise, it interns the atom and returns it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom to ensure is interned.
 * @return A pointer to the interned atom.
 */
static inline REDUCT_ALWAYS_INLINE reduct_atom_t* reduct_atom_ensure_interned(struct reduct* reduct,
    reduct_atom_t* atom)
{
    if (REDUCT_LIKELY(atom->index != REDUCT_ATOM_INDEX_NONE))
    {
        return atom;
    }

    if (REDUCT_LIKELY(reduct_atom_intern(reduct, atom)))
    {
        return atom;
    }

    return reduct_atom_lookup(reduct, atom->string, atom->length, reduct_atom_get_lookup_flags(atom));
}

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
 * @brief Retain an atom, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 */
REDUCT_API void reduct_atom_retain(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Release an atom, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 */
REDUCT_API void reduct_atom_release(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Check if an atom is an intrinsic.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 * @return `true` if the atom is an intrinsic, `false` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_atom_is_intrinsic(struct reduct* reduct, reduct_atom_t* atom)
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
 * @return `true` if the atom is a native function, `false` otherwise.
 */
static inline bool reduct_atom_is_native(struct reduct* reduct, reduct_atom_t* atom)
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
 * @return `true` if the atom is an integer, `false` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_atom_is_int(reduct_atom_t* atom)
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
 * @return `true` if the atom is a float, `false` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_atom_is_float(reduct_atom_t* atom)
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
 * @return `true` if the atom is a number, `false` otherwise.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_atom_is_number(reduct_atom_t* atom)
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
static inline REDUCT_ALWAYS_INLINE int64_t reduct_atom_get_int(reduct_atom_t* atom)
{
    assert(reduct_atom_is_number(atom));

    if (reduct_atom_is_int(atom))
    {
        return atom->integerValue;
    }
    else
    {
        return (int64_t)atom->floatValue;
    }
}

/**
 * @brief Get the float value of an atom.
 *
 * @param atom Pointer to the atom.
 * @return The float value.
 */
static inline REDUCT_ALWAYS_INLINE double reduct_atom_get_float(reduct_atom_t* atom)
{
    assert(reduct_atom_is_number(atom));

    if (reduct_atom_is_float(atom))
    {
        return atom->floatValue;
    }
    else
    {
        return (double)atom->integerValue;
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
REDUCT_API reduct_atom_t* reduct_atom_substr(struct reduct* reduct, reduct_atom_t* atom, size_t start, size_t len);

/**
 * @brief Create a superstring of an existing atom.
 *
 * If the atom is at the end of its stack and there is enough capacity, it will extend the existing allocation.
 * Otherwise, it will allocate a new atom and copy the existing data.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the source atom.
 * @param len The new total length.
 * @return A pointer to the new atom.
 */
REDUCT_API reduct_atom_t* reduct_atom_superstr(struct reduct* reduct, reduct_atom_t* atom, size_t len);

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
    size_t len)
{
    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, data, len);
    return atom;
}

/**
 * @brief Retrieve an integer value from an atom, regardless of if it is quoted or not.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 * @return The integer value.
 */
REDUCT_API int64_t reduct_atom_as_int(struct reduct* reduct, reduct_atom_t* atom);

/**
 * @brief Retrieve a float value from an atom, regardless of if it is quoted or not.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param atom Pointer to the atom.
 * @return The float value.
 */
REDUCT_API double reduct_atom_as_float(struct reduct* reduct, reduct_atom_t* atom);

/** @} */

#endif
