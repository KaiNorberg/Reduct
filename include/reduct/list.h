#ifndef REDUCT_LIST_H
#define REDUCT_LIST_H 1

#include <reduct/arena.h>
#include <reduct/defs.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

struct reduct;
struct reduct_item;

/**
 * @file list.h
 * @brief List management.
 * @defgroup list List
 *
 * A list is a sequence of handles. Small lists are stored directly within the item,
 * while larger lists are allocated from an arena.
 *
 * ## Overlapping List Optimization
 *
 * When creating new lists by appending or prepending elements to existing lists, Reduct will attempt to reuse the
 * existing list.
 *
 * This is done by allocating additional "tailroom" and "headroom" when creating large lists. When appending or
 * prepending, if the existing list has unused tailroom or headroom, a new list can be created that overlaps with the
 * existing list's buffer, avoiding the need to copy elements.
 *
 * This in combination with the fact that the structure is an contiguous array makes random-access, appending,
 * prepending, sliceing and concatenation `O(1)` in the common case. With appending, prepending and concatenation being
 * `O(n)` in the worse case. While also providing good caching and list iteration.
 *
 */

#define REDUCT_LIST_SMALL_MAX 4         ///< The maximum number of elements in a small list.
#define REDUCT_LIST_LARGE_MIN 16        ///< The minimum number of elements in a large list.
#define REDUCT_LIST_EXTRA_ROOM_FACTOR 2 ///< The factor of extra capacity to allocate around large lists.

typedef uint8_t reduct_list_flags_t;
#define REDUCT_LIST_FLAG_NONE 0             ///< No flags.
#define REDUCT_LIST_FLAG_LARGE (1 << 0)     ///< List has an allocated buffer within an arena.
#define REDUCT_LIST_FLAG_USED_HEAD (1 << 1) ///< Reserved headroom has been used by a derivative list.
#define REDUCT_LIST_FLAG_USED_TAIL (1 << 2) ///< Reserved tailroom has been used by a derivative list.

/**
 * @brief List structure.
 * @struct reduct_list_t
 */
typedef struct reduct_list
{
    uint32_t length;   ///< The length of the list (must be first, check the `reduct_item_t` structure).
    uint32_t capacity; ///< The current capacity of the handle buffer.
    uint32_t offset;   ///< Offset of elements from the start of the buffer.
    _Atomic(reduct_list_flags_t) flags; ///< List flags.
    uint8_t _padding[3];
    reduct_handle_t* handles; ///< Pointer to the handle data.
    union {
        reduct_handle_t smallHandles[REDUCT_LIST_SMALL_MAX]; ///< Small list data.
        reduct_arena_t* arena;                               ///< The arena this list was allocated from.
    };
} reduct_list_t;

/**
 * @brief A key-value pair for creating association lists.
 * @struct reduct_list_entry_t
 */
typedef struct
{
    const char* key;
    reduct_handle_t value;
} reduct_list_entry_t;

/**
 * @brief Create a new editable list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param length The predetermined length of the list.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_new(struct reduct* reduct, size_t length);

/**
 * @brief Create a new list from an array of handles.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count The number of handles.
 * @param handles The array of handles.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_new_handles(struct reduct* reduct, size_t count, reduct_handle_t* handles);

/**
 * @brief Create a new association list (list storing key-value pairs) from a variable number of pairs.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count The number of pairs.
 * @param ... Each pair should be provided as a `(const char*, reduct_handle_t)`.
 * @return A pointer to the newly created association list.
 */
REDUCT_API reduct_list_t* reduct_list_new_alist(struct reduct* reduct, size_t count, ...);

/**
 * @brief Create a new association list from an array of entries.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count Number of entries.
 * @param entries Array of key-value pairs.
 */
REDUCT_API reduct_list_t* reduct_list_new_alist_entries(struct reduct* reduct, size_t count,
    const reduct_list_entry_t* entries);

/**
 * @brief Create a new list by slicing an existing list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the source list.
 * @param start The starting index (inclusive).
 * @param end The ending index (exclusive).
 * @return A pointer to the newly created list slice.
 */
REDUCT_API reduct_list_t* reduct_list_slice(struct reduct* reduct, reduct_list_t* list, size_t start, size_t end);

/**
 * @brief Create a new list by appending an element to an existing list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the source list.
 * @param val The value to append.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_append(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val);

/**
 * @brief Create a new list by prepending an element to an existing list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the source list.
 * @param val The value to prepend.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_prepend(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val);

/**
 * @brief Create a new list by concatenating two existing lists.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param a Pointer to the first list.
 * @param b Pointer to the second list.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_concat(struct reduct* reduct, reduct_list_t* a, reduct_list_t* b);

/**
 * @brief Retain a list, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list, can be `NULL`.
 */
REDUCT_API void reduct_list_retain(struct reduct* reduct, reduct_list_t* list);

/**
 * @brief Release a list, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list, can be `NULL`.
 */
REDUCT_API void reduct_list_release(struct reduct* reduct, reduct_list_t* list);

/** @} */

#endif
