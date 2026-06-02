#include <reduct/core.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/list.h>

#include <stdarg.h>

static reduct_list_node_t* reduct_list_node_new(struct reduct* reduct)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_LIST_NODE;
    return &item->listNode;
}

static reduct_list_node_t* reduct_list_node_copy(reduct_t* reduct, reduct_list_node_t* node)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_LIST_NODE;
    reduct_list_node_t* newNode = &item->listNode;
    memcpy(newNode, node, sizeof(reduct_list_node_t));
    return newNode;
}

static inline void reduct_list_node_init(reduct_list_node_t* node)
{
    for (uint32_t i = 0; i < REDUCT_LIST_WIDTH; i++)
    {
        node->children[i] = NULL;
    }
}

REDUCT_API reduct_list_t* reduct_list_new(reduct_t* reduct)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_LIST;
    reduct_list_t* list = &item->list;
    list->length = 0;
    list->shift = 0;
    list->root = NULL;
    reduct_list_node_init(&list->tail);
    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_handles(struct reduct* reduct, size_t count, reduct_handle_t* handles)
{
    assert(reduct != NULL);
    assert(handles != NULL || count == 0);

    if (REDUCT_LIKELY(count <= REDUCT_LIST_WIDTH))
    {
        reduct_item_t* item = reduct_item_new(reduct);
        item->type = REDUCT_ITEM_TYPE_LIST;

        reduct_list_t* list = &item->list;
        list->length = count;
        list->shift = 0;
        list->root = NULL;
        for (uint32_t i = 0; i < count; i++)
        {
            list->tail.handles[i] = handles[i];
        }
        for (uint32_t i = count; i < REDUCT_LIST_WIDTH; i++)
        {
            list->tail.handles[i] = (reduct_handle_t){0};
        }
        return list;
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (uint32_t i = 0; i < count; i++)
    {
        reduct_list_push(reduct, list, handles[i]);
    }

    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_alist(struct reduct* reduct, size_t count, ...)
{
    assert(reduct != NULL);

    reduct_list_t* list = reduct_list_new(reduct);
    va_list args;
    va_start(args, count);

    for (size_t i = 0; i < count; i++)
    {
        const char* key = va_arg(args, const char*);
        reduct_handle_t val = va_arg(args, reduct_handle_t);

        reduct_list_t* pair = reduct_list_new(reduct);
        reduct_list_push(reduct, pair, REDUCT_HANDLE_CREATE_STRING(reduct, key));
        reduct_list_push(reduct, pair, val);
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_LIST(pair));
    }

    va_end(args);
    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_alist_entries(struct reduct* reduct, size_t count,
    const reduct_list_entry_t* entries)
{
    assert(reduct != NULL);
    assert(entries != NULL || count == 0);

    reduct_list_t* list = reduct_list_new(reduct);
    for (size_t i = 0; i < count; i++)
    {
        reduct_list_t* pair = reduct_list_new(reduct);
        reduct_list_push(reduct, pair, REDUCT_HANDLE_CREATE_STRING(reduct, entries[i].key));
        reduct_list_push(reduct, pair, entries[i].value);
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_LIST(pair));
    }

    return list;
}

REDUCT_API reduct_list_node_t* reduct_list_find_leaf(reduct_list_t* list, size_t index, size_t tailOffset)
{
    if (REDUCT_LIKELY(index >= tailOffset))
    {
        return &list->tail;
    }

    reduct_list_node_t* node = list->root;
    uint32_t level = list->shift;

    while (level > 0)
    {
        node = node->children[(index >> level) & REDUCT_LIST_MASK];
        level -= REDUCT_LIST_BITS;
    }

    return node;
}

static inline reduct_list_node_t* reduct_list_assoc_internal(reduct_t* reduct, uint32_t shift, reduct_list_node_t* node,
    size_t index, reduct_handle_t val)
{
    reduct_list_node_t* newNode = reduct_list_node_copy(reduct, node);
    if (shift == 0)
    {
        newNode->handles[index & REDUCT_LIST_MASK] = val;
    }
    else
    {
        uint32_t subIdx = (index >> shift) & REDUCT_LIST_MASK;
        newNode->children[subIdx] =
            reduct_list_assoc_internal(reduct, shift - REDUCT_LIST_BITS, newNode->children[subIdx], index, val);
    }
    return newNode;
}

REDUCT_API reduct_list_t* reduct_list_assoc(struct reduct* reduct, reduct_list_t* list, size_t index,
    reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (REDUCT_UNLIKELY(index >= list->length))
    {
        REDUCT_ERROR_THROW(reduct, "index %zu out of bounds for list of length %u", index);
    }

    reduct_list_t* newList = reduct_list_new(reduct);
    newList->length = list->length;
    newList->shift = list->shift;

    size_t tailOffset = REDUCT_LIST_TAIL_OFFSET(list);

    if (index >= tailOffset)
    {
        newList->root = list->root;
        newList->tail = list->tail;
        newList->tail.handles[index & REDUCT_LIST_MASK] = val;
    }
    else
    {
        newList->root = reduct_list_assoc_internal(reduct, list->shift, list->root, index, val);
        newList->tail = list->tail;
    }

    return newList;
}

REDUCT_API reduct_list_t* reduct_list_dissoc(struct reduct* reduct, reduct_list_t* list, size_t index)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (REDUCT_UNLIKELY(index >= list->length))
    {
        return list;
    }

    /// @todo There is definetly a better way to do this

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, list)
    {
        if (_index != index)
        {
            reduct_list_push(reduct, newList, handle);
        }
    }

    return newList;
}

REDUCT_API reduct_list_t* reduct_list_slice(struct reduct* reduct, reduct_list_t* list, size_t start, size_t end)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (REDUCT_UNLIKELY(start > end || end > list->length))
    {
        REDUCT_ERROR_THROW(reduct, "slice: invalid range [%zu, %zu) for list of length %u", start, end, list->length);
    }

    reduct_list_t* newList = reduct_list_new(reduct);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH_AT(&handle, list, start)
    {
        if (_index >= end)
            break;

        reduct_list_push(reduct, newList, handle);
    }

    return newList;
}

REDUCT_API reduct_list_t* reduct_list_append(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_list_t* newList = reduct_list_new(reduct);
    newList->length = list->length;
    newList->shift = list->shift;
    newList->root = list->root;
    newList->tail = list->tail;

    reduct_list_push(reduct, newList, val);
    return newList;
}

REDUCT_API reduct_list_t* reduct_list_prepend(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_list_push(reduct, newList, val);
    reduct_list_push_list(reduct, newList, list);

    return newList;
}

REDUCT_API reduct_handle_t reduct_list_nth(struct reduct* reduct, reduct_list_t* list, size_t index)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (list->length <= REDUCT_LIST_WIDTH)
    {
        return list->tail.handles[index];
    }

    if (REDUCT_UNLIKELY(index >= list->length))
    {
        REDUCT_ERROR_THROW(reduct, "index %zu out of bounds for list of length %u", index);
    }

    size_t tailOffset = REDUCT_LIST_TAIL_OFFSET(list);
    reduct_list_node_t* node = reduct_list_find_leaf(list, index, tailOffset);
    return node->handles[index & REDUCT_LIST_MASK];
}

REDUCT_API struct reduct_item* reduct_list_nth_item(struct reduct* reduct, reduct_list_t* list, size_t index)
{
    reduct_handle_t handle = reduct_list_nth(reduct, list, index);
    reduct_handle_ensure_item(reduct, &handle);
    return REDUCT_HANDLE_TO_ITEM(handle);
}

static reduct_list_node_t* reduct_push_tail(reduct_t* reduct, uint32_t shift, size_t index, reduct_list_node_t* parent,
    reduct_list_node_t* tailNode)
{
    assert(reduct != NULL);
    assert(tailNode != NULL);

    if (shift == 0)
    {
        return tailNode;
    }

    reduct_list_node_t* newNode = parent != NULL ? reduct_list_node_copy(reduct, parent) : reduct_list_node_new(reduct);
    if (parent == NULL)
    {
        reduct_list_node_init(newNode);
    }
    uint32_t subIdx = (index >> shift) & REDUCT_LIST_MASK;
    newNode->children[subIdx] =
        reduct_push_tail(reduct, shift - REDUCT_LIST_BITS, index, newNode->children[subIdx], tailNode);
    return newNode;
}

REDUCT_API void reduct_list_push(reduct_t* reduct, reduct_list_t* list, reduct_handle_t val)
{
    assert(list != NULL);

    if (list->length < REDUCT_LIST_WIDTH || (list->length & REDUCT_LIST_MASK) != 0)
    {
        list->tail.handles[list->length & REDUCT_LIST_MASK] = val;
        list->length++;
        return;
    }

    reduct_list_node_t* fullTailNode = reduct_list_node_copy(reduct, &list->tail);
    reduct_list_node_init(&list->tail);
    list->tail.handles[0] = val;

    if (list->root == NULL)
    {
        list->root = fullTailNode;
        list->shift = 0;
        list->length++;
        return;
    }

    if ((list->length - 1) >> (list->shift + REDUCT_LIST_BITS) > 0)
    {
        reduct_list_node_t* newRoot = reduct_list_node_new(reduct);
        reduct_list_node_init(newRoot);
        newRoot->children[0] = list->root;
        newRoot->children[1] = reduct_push_tail(reduct, list->shift, list->length - 1, NULL, fullTailNode);
        list->root = newRoot;
        list->shift += REDUCT_LIST_BITS;
        list->length++;
        return;
    }

    list->root = reduct_push_tail(reduct, list->shift, list->length - 1, list->root, fullTailNode);
    list->length++;
}

REDUCT_API void reduct_list_push_list(reduct_t* reduct, reduct_list_t* list, reduct_list_t* other)
{
    assert(reduct != NULL);
    assert(list != NULL);
    assert(other != NULL);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, other)
    {
        reduct_list_push(reduct, list, handle);
    }
}

REDUCT_API size_t reduct_list_to_handles(reduct_list_t* list, reduct_handle_t* out, size_t capacity)
{
    assert(list != NULL);
    assert(out != NULL || capacity == 0);

    size_t totalCopied = 0;
    size_t toCopy = list->length < capacity ? list->length : capacity;

    reduct_list_iter_t iter = REDUCT_LIST_ITER(list);
    reduct_list_chunk_t chunk;
    while (reduct_list_iter_next_chunk(&iter, &chunk) && totalCopied < toCopy)
    {
        size_t n = chunk.count;
        if (totalCopied + n > toCopy)
        {
            n = toCopy - totalCopied;
        }
        memcpy(out + totalCopied, chunk.handles, n * sizeof(reduct_handle_t));
        totalCopied += n;
    }
    return totalCopied;
}

REDUCT_API reduct_handle_t reduct_list_find_entry(reduct_t* reduct, reduct_list_t* list, reduct_handle_t key)
{
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(key), "key must be an atom");

    reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));

    reduct_handle_t entryH;
    REDUCT_LIST_FOR_EACH(&entryH, list)
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(entryH)))
        {
            continue;
        }

        reduct_list_t* entry = REDUCT_HANDLE_TO_LIST(entryH);
        if (REDUCT_UNLIKELY(entry->length < 1))
        {
            continue;
        }

        reduct_handle_t entryKey = reduct_list_first(reduct, entry);
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ATOM(entryKey)))
        {
            continue;
        }

        reduct_atom_t* entryKeyInterned = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(entryKey));
        if (internedKey == entryKeyInterned)
        {
            return entryH;
        }
    }

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_list_get(struct reduct* reduct, reduct_list_t* list, const char* key)
{
    assert(reduct != NULL);
    assert(list != NULL);
    assert(key != NULL);

    reduct_handle_t keyH = REDUCT_HANDLE_CREATE_SYMBOL(reduct, key);
    reduct_handle_t entryH = reduct_list_find_entry(reduct, list, keyH);

    if (REDUCT_HANDLE_IS_NIL(entryH) || REDUCT_HANDLE_IS_NIL(entryH))
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    return (REDUCT_HANDLE_TO_LIST(entryH)->length >= 2) ? reduct_list_second(reduct, REDUCT_HANDLE_TO_LIST(entryH))
                                                        : REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API bool reduct_list_find_entry_index(reduct_t* reduct, reduct_list_t* list, reduct_handle_t key,
    size_t* outIndex)
{
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(key), "key must be an atom");

    reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));

    reduct_handle_t entryH;
    REDUCT_LIST_FOR_EACH(&entryH, list)
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(entryH)))
        {
            continue;
        }

        reduct_list_t* entry = REDUCT_HANDLE_TO_LIST(entryH);
        if (REDUCT_UNLIKELY(entry->length < 1))
        {
            continue;
        }

        reduct_handle_t entryKey = reduct_list_first(reduct, entry);
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ATOM(entryKey)))
        {
            continue;
        }

        reduct_atom_t* entryKeyInterned = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(entryKey));
        if (internedKey == entryKeyInterned)
        {
            if (outIndex != NULL)
            {
                *outIndex = _index;
            }
            return true;
        }
    }

    return false;
}

REDUCT_API bool reduct_list_get_entry(reduct_t* reduct, reduct_handle_t entryH, reduct_handle_t* outKey,
    reduct_handle_t* outVal)
{
    if (!REDUCT_HANDLE_IS_LIST(entryH))
    {
        return false;
    }
    reduct_item_t* entry = REDUCT_HANDLE_TO_ITEM(entryH);
    if (entry->length < 1)
    {
        return false;
    }

    if (outKey != NULL)
    {
        *outKey = reduct_list_first(reduct, &entry->list);
    }

    if (outVal != NULL)
    {
        *outVal = (entry->length >= 2) ? reduct_list_second(reduct, &entry->list) : REDUCT_HANDLE_NIL(reduct);
    }
    return true;
}

REDUCT_API void reduct_list_retain(reduct_t* reduct, reduct_list_t* list)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_gc_retain(reduct, REDUCT_CONTAINER_OF(list, reduct_item_t, list));
}

REDUCT_API void reduct_list_release(reduct_t* reduct, reduct_list_t* list)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_gc_release(reduct, REDUCT_CONTAINER_OF(list, reduct_item_t, list));
}
