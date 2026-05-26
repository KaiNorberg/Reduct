#ifndef REDUCT_LIST_H
#define REDUCT_LIST_H 1

#include <reduct/defs.h>

#include <stdbool.h>
#include <stddef.h>

struct reduct;
struct reduct_item;

/**
 * @file list.h
 * @brief List management.
 * @defgroup list List
 *
 * A list is a persistent data structure implemented using a bit-mapped vector trie.
 *
 * The list is made up of a "tree" of nodes along with a "tail" node, with each node storing a fixed size array of
 * either children or elements within the list.
 *
 * When an element is added to a list, it will be appended to the array within the tail node, once the tail node is
 * full, it is "pushed" to the front of the tree, which may require increasing the depth of tree.
 *
 * @see [Persistent vectors, Part 2 -- Immutability and persistence]
 * (https://dmiller.github.io/clojure-clr-next/general/2023/02/12/PersistentVector-part-2.html)
 *
 */

#define REDUCT_LIST_BITS 2                        ///< Number of bits per level in the trie.
#define REDUCT_LIST_WIDTH (1 << REDUCT_LIST_BITS) ///< The number of children per node.
#define REDUCT_LIST_MASK (REDUCT_LIST_WIDTH - 1)  ///< Mask for the index at each level.

/**
 * @brief List node structure.
 * @struct reduct_list_node_t
 */
typedef struct reduct_list_node
{
    union {
        struct reduct_list_node* children[REDUCT_LIST_WIDTH];
        reduct_handle_t handles[REDUCT_LIST_WIDTH];
    };
} reduct_list_node_t;

/**
 * @brief List structure.
 * @struct reduct_list_t
 */
typedef struct reduct_list
{
    uint32_t length;          ///< Total number of elements.
    uint32_t shift;           ///< The amount to shift the index to compute access paths.
    reduct_list_node_t* root; ///< Pointer to the trie root node.
    reduct_list_node_t tail;  ///< The tail node, stored inline.
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
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_new(struct reduct* reduct);

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
 * @brief Create a new list with an updated value at the specified index.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the source list.
 * @param index The index to update.
 * @param val The new value to set.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_assoc(struct reduct* reduct, reduct_list_t* list, size_t index,
    reduct_handle_t val);

/**
 * @brief Create a new list with the element at the specified index removed.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the source list.
 * @param index The index of the element to remove.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_dissoc(struct reduct* reduct, reduct_list_t* list, size_t index);

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
 * @brief Get the nth element of the list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @param index The index of the element to retrieve.
 * @return The handle of the nth element.
 */
REDUCT_API reduct_handle_t reduct_list_nth(struct reduct* reduct, reduct_list_t* list, size_t index);

/**
 * @brief Get the nth element of the list as an item.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @param index The index of the element to retrieve.
 * @return A pointer to the item of the nth element.
 */
REDUCT_API struct reduct_item* reduct_list_nth_item(struct reduct* reduct, reduct_list_t* list, size_t index);

/**
 * @brief Push a new element to the list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list The target list (must be editable).
 * @param val Handle to the value to append.
 */
REDUCT_API void reduct_list_push(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val);

/**
 * @brief Push all elements from one list to another.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list The target list (must be editable).
 * @param other The source list to copy from.
 */
REDUCT_API void reduct_list_push_list(struct reduct* reduct, reduct_list_t* list, reduct_list_t* other);

/**
 * @brief Copy elements from a list into a C array of handles.
 *
 * @param list Pointer to the list.
 * @param out Pointer to the destination array.
 * @param capacity The maximum number of handles to copy.
 * @return The number of handles actually copied.
 */
REDUCT_API size_t reduct_list_to_handles(reduct_list_t* list, reduct_handle_t* out, size_t capacity);

/**
 * @brief Find the leaf node containing the element at the specified index.
 *
 * @param list Pointer to the list.
 * @param index The index of the element.
 * @param tailOffset The pre-calculated offset of the tail node.
 * @return A pointer to the leaf node.
 */
REDUCT_API reduct_list_node_t* reduct_list_find_leaf(reduct_list_t* list, size_t index, size_t tailOffset);

/**
 * @brief A contiguous chunk of handles from a list.
 * @struct reduct_list_chunk_t
 */
typedef struct
{
    reduct_handle_t* handles; ///< Pointer to the first handle in the chunk.
    size_t count;             ///< Number of handles in the chunk.
} reduct_list_chunk_t;

/**
 * @brief List iterator structure.
 * @struct reduct_list_iter_t
 */
typedef struct reduct_list_iter
{
    reduct_list_t* list;
    size_t index;
    size_t tailOffset;
    reduct_handle_t* chunkPtr;
    uint32_t chunkRem;
} reduct_list_iter_t;

/**
 * @brief Calculate the offset of the tail node.
 *
 * @param _list Pointer to the list.
 */
#define REDUCT_LIST_TAIL_OFFSET(_list) (((_list)->length > 0) ? (((_list)->length - 1) & ~REDUCT_LIST_MASK) : 0)

/**
 * @brief Create a initializer for a list iterator.
 *
 * @param _list The list to iterate over.
 */
#define REDUCT_LIST_ITER(_list) ((reduct_list_iter_t){(_list), 0, REDUCT_LIST_TAIL_OFFSET(_list), NULL, 0})

/**
 * @brief Create a initializer for a list iterator start at a specific index.
 *
 * @param _list The list to iterate over.
 * @param _start The starting index.
 */
#define REDUCT_LIST_ITER_AT(_list, _start) \
    ((reduct_list_iter_t){(_list), (_start), REDUCT_LIST_TAIL_OFFSET(_list), NULL, 0})

/**
 * @brief Get the next chunk of elements from the iterator.
 *
 * @param iter Pointer to the iterator.
 * @param out Pointer to store the retrieved chunk info.
 * @return `true` if a chunk was retrieved, `false` if the end was reached.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_list_iter_next_chunk(reduct_list_iter_t* iter, reduct_list_chunk_t* out)
{
    if (REDUCT_UNLIKELY(iter->index >= iter->list->length))
    {
        return false;
    }

    reduct_list_node_t* leaf = reduct_list_find_leaf(iter->list, iter->index, iter->tailOffset);
    size_t offset = iter->index & REDUCT_LIST_MASK;

    out->handles = &leaf->handles[offset];

    size_t nodeRem = REDUCT_LIST_WIDTH - offset;
    size_t listRem = (size_t)iter->list->length - iter->index;
    out->count = nodeRem < listRem ? nodeRem : listRem;

    iter->index += out->count;
    iter->chunkPtr = NULL;
    iter->chunkRem = 0;
    return true;
}

/**
 * @brief Get the next element from the iterator.
 *
 * @param iter Pointer to the iterator.
 * @param out Pointer to store the retrieved handle.
 * @return `true` if an element was retrieved, `false` if the end was reached.
 */
static inline REDUCT_ALWAYS_INLINE bool reduct_list_iter_next(reduct_list_iter_t* iter, reduct_handle_t* out)
{
    if (REDUCT_UNLIKELY(iter->chunkRem == 0))
    {
        if (REDUCT_UNLIKELY(iter->index >= iter->list->length))
        {
            return false;
        }

        reduct_list_node_t* leaf = reduct_list_find_leaf(iter->list, iter->index, iter->tailOffset);
        size_t offset = iter->index & REDUCT_LIST_MASK;
        iter->chunkPtr = &leaf->handles[offset];

        size_t nodeRem = REDUCT_LIST_WIDTH - offset;
        size_t listRem = (size_t)iter->list->length - iter->index;
        iter->chunkRem = (uint32_t)(nodeRem < listRem ? nodeRem : listRem);
    }

    *out = *iter->chunkPtr++;
    iter->index++;
    iter->chunkRem--;
    return true;
}

/**
 * @brief Macro for iterating over all elements in a list.
 *
 * @param _handle The reduct_handle_t variable to store each element.
 * @param _list Pointer to the reduct_list_t to iterate.
 */
#define REDUCT_LIST_FOR_EACH(_handle, _list) \
    for (reduct_list_iter_t _iter = REDUCT_LIST_ITER(_list); reduct_list_iter_next(&_iter, (_handle));)

/**
 * @brief Macro for iterating over elements in a list starting from a specific index.
 *
 * @param _handle The reduct_handle_t variable to store each element.
 * @param _list Pointer to the reduct_list_t to iterate.
 * @param _start The starting index.
 */
#define REDUCT_LIST_FOR_EACH_AT(_handle, _list, _start) \
    for (reduct_list_iter_t _iter = REDUCT_LIST_ITER_AT(_list, _start); reduct_list_iter_next(&_iter, (_handle));)

/**
 * @brief Get the first element of the list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @return The handle of the first element.
 */
static inline REDUCT_ALWAYS_INLINE reduct_handle_t reduct_list_first(struct reduct* reduct, reduct_list_t* list)
{
    if (REDUCT_LIKELY(list->length <= REDUCT_LIST_WIDTH))
    {
        return list->tail.handles[0];
    }
    return reduct_list_nth(reduct, list, 0);
}

/**
 * @brief Get the second element of the list
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @return The handle of the second element.
 */
static inline REDUCT_ALWAYS_INLINE reduct_handle_t reduct_list_second(struct reduct* reduct, reduct_list_t* list)
{
    if (REDUCT_LIKELY(list->length <= REDUCT_LIST_WIDTH))
    {
        return list->tail.handles[1];
    }
    return reduct_list_nth(reduct, list, 1);
}

/**
 * @brief Find an entry in a association list by its key.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @param key Pointer to the key handle.
 * @return The handle of the entry list, or `REDUCT_HANDLE_NIL(reduct)` if not found.
 */
REDUCT_API reduct_handle_t reduct_list_find_entry(struct reduct* reduct, reduct_list_t* list, reduct_handle_t key);

/**
 * @brief High-level helper to get a value from an association list by a C string key.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @param key The key string to look up.
 * @return The value handle, or `REDUCT_HANDLE_NIL(reduct)` if not found.
 */
REDUCT_API reduct_handle_t reduct_list_get(struct reduct* reduct, reduct_list_t* list, const char* key);

/**
 * @brief Find the index of an entry in an association list by its key.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 * @param key Pointer to the key handle.
 * @param outIndex Pointer to store the index of the entry if found.
 * @return `true` if the entry was found, `false` otherwise.
 */
REDUCT_API bool reduct_list_find_entry_index(struct reduct* reduct, reduct_list_t* list, reduct_handle_t key,
    size_t* outIndex);

/**
 * @brief Get the key and value from an entry in a association list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param entryH Pointer to the entry handle, must be a list.
 * @param outKey Pointer to store the key handle, can be `NULL`.
 * @param outVal Pointer to store the value handle, can be `NULL`.
 * @return `true` if the entry is valid and handles were retrieved, `false` otherwise.
 */
REDUCT_API bool reduct_list_get_entry(struct reduct* reduct, reduct_handle_t entryH, reduct_handle_t* outKey,
    reduct_handle_t* outVal);

/**
 * @brief Retain a list, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 */
REDUCT_API void reduct_list_retain(struct reduct* reduct, reduct_list_t* list);

/**
 * @brief Release a list, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param list Pointer to the list.
 */
REDUCT_API void reduct_list_release(struct reduct* reduct, reduct_list_t* list);

/** @} */

#endif
