#ifndef REDUCT_LIST_H
#define REDUCT_LIST_H 1

struct reduct;
struct reduct_item;

#include "reduct/defs.h"

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
    uint32_t length;   ///< Total number of elements.
    uint32_t shift;    ///< The amount to shift the index to compute access paths.
    reduct_list_node_t* root; ///< Pointer to the trie root node.
    reduct_list_node_t tail;  ///< The tail node, stored inline.
} reduct_list_t;

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
REDUCT_API reduct_list_t* reduct_list_slice(struct reduct* reduct, reduct_list_t* list, size_t start,
    size_t end);

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
 * @brief Create a new list from an array of handles.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count The number of handles.
 * @param handles The array of handles.
 * @return A pointer to the newly created list.
 */
REDUCT_API reduct_list_t* reduct_list_new_from_handles(struct reduct* reduct, size_t count, reduct_handle_t* handles);

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
 * @brief List iterator structure.
 * @struct reduct_list_iter_t
 */
typedef struct reduct_list_iter
{
    reduct_list_t* list;
    size_t index;
    reduct_list_node_t* leaf;
    size_t tailOffset;
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
#define REDUCT_LIST_ITER(_list) {(_list), 0, NULL, REDUCT_LIST_TAIL_OFFSET(_list)}

/**
 * @brief Create a initializer for a list iterator start at a specific index.
 *
 * @param _list The list to iterate over.
 * @param _start The starting index.
 */
#define REDUCT_LIST_ITER_AT(_list, _start) {(_list), (_start), NULL, REDUCT_LIST_TAIL_OFFSET(_list)}

/**
 * @brief Get the next element from the iterator.
 *
 * @param iter Pointer to the iterator.
 * @param out Pointer to store the retrieved handle.
 * @return `REDUCT_TRUE` if an element was retrieved, `REDUCT_FALSE` if the end was reached.
 */
static inline REDUCT_ALWAYS_INLINE reduct_bool_t reduct_list_iter_next(reduct_list_iter_t* iter, reduct_handle_t* out)
{
    if (iter->leaf != NULL)
    {
        iter->index++;
    }

    if (REDUCT_UNLIKELY(iter->index >= iter->list->length))
    {
        return REDUCT_FALSE;
    }

    if ((iter->index & REDUCT_LIST_MASK) == 0 || iter->leaf == NULL)
    {
        iter->leaf = reduct_list_find_leaf(iter->list, iter->index, iter->tailOffset);
    }

    *out = iter->leaf->handles[iter->index & REDUCT_LIST_MASK];
    return REDUCT_TRUE;
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
 * @param listItem Pointer to the list item.
 * @param key Pointer to the key handle.
 * @return The handle of the entry list, or `REDUCT_HANDLE_NONE` if not found.
 */
REDUCT_API reduct_handle_t reduct_list_find_entry(struct reduct* reduct, struct reduct_item* listItem, reduct_handle_t* key);

/**
 * @brief Get the key and value from an entry in a association list.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param entryH Pointer to the entry handle, must be a list.
 * @param outKey Pointer to store the key handle, can be `NULL`.
 * @param outVal Pointer to store the value handle, can be `NULL`.
 * @return `REDUCT_TRUE` if the entry is valid and handles were retrieved, `REDUCT_FALSE` otherwise.
 */
REDUCT_API reduct_bool_t reduct_list_get_entry(struct reduct* reduct, reduct_handle_t* entryH, reduct_handle_t* outKey,
    reduct_handle_t* outVal);

#endif
