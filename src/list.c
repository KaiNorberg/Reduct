#include "reduct/arena.h"
#include <reduct/core.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/list.h>

#include <stdarg.h>

REDUCT_API reduct_list_t* reduct_list_new(reduct_t* reduct, size_t length)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_LIST;
    reduct_list_t* list = &item->list;
    list->length = (uint32_t)length;

    if (length <= REDUCT_LIST_SMALL_MAX)
    {
        list->handles = list->smallHandles;
        list->capacity = REDUCT_LIST_SMALL_MAX;
        list->offset = 0;
        atomic_init(&list->flags, REDUCT_LIST_FLAG_NONE);
    }
    else
    {
        size_t capacity = REDUCT_MAX(length, REDUCT_LIST_LARGE_MIN) * REDUCT_LIST_EXTRA_ROOM_FACTOR;
        size_t headroom = (capacity - length) / (REDUCT_LIST_EXTRA_ROOM_FACTOR * 2);

        reduct_arena_chunk_t chunk;
        reduct_arena_alloc(reduct, capacity * sizeof(reduct_handle_t), &chunk);
        list->handles = (reduct_handle_t*)chunk.data + headroom;
        list->capacity = (uint32_t)capacity;
        list->offset = (uint32_t)headroom;
        list->arena = chunk.arena;
        atomic_init(&list->flags, REDUCT_LIST_FLAG_LARGE);
    }

    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_handles(struct reduct* reduct, size_t count, reduct_handle_t* handles)
{
    assert(reduct != NULL);
    assert(handles != NULL || count == 0);

    reduct_list_t* list = reduct_list_new(reduct, count);
    if (count > 0 && handles != NULL)
    {
        memcpy(list->handles, handles, count * sizeof(reduct_handle_t));
    }

    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_alist(struct reduct* reduct, size_t count, ...)
{
    assert(reduct != NULL);

    reduct_list_t* list = reduct_list_new(reduct, count);
    va_list args;
    va_start(args, count);

    for (size_t i = 0; i < count; i++)
    {
        const char* key = va_arg(args, const char*);
        reduct_handle_t val = va_arg(args, reduct_handle_t);

        reduct_list_t* pair = reduct_list_new(reduct, 2);
        pair->handles[0] = REDUCT_HANDLE_CREATE_STRING(reduct, key);
        pair->handles[1] = val;

        list->handles[i] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    va_end(args);
    return list;
}

REDUCT_API reduct_list_t* reduct_list_new_alist_entries(struct reduct* reduct, size_t count,
    const reduct_list_entry_t* entries)
{
    assert(reduct != NULL);
    assert(entries != NULL || count == 0);

    reduct_list_t* list = reduct_list_new(reduct, count);
    for (size_t i = 0; i < count; i++)
    {
        reduct_list_t* pair = reduct_list_new(reduct, 2);
        pair->handles[0] = REDUCT_HANDLE_CREATE_STRING(reduct, entries[i].key);
        pair->handles[1] = entries[i].value;

        list->handles[i] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    return list;
}

REDUCT_API reduct_list_t* reduct_list_slice(struct reduct* reduct, reduct_list_t* list, size_t start, size_t end)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (REDUCT_UNLIKELY(start > end || end > list->length))
    {
        REDUCT_ERROR_THROW(reduct, "slice: invalid range [%zu, %zu) for list of length %u", start, end, list->length);
    }

    size_t len = end - start;
    if (len == 0)
    {
        return reduct_list_new(reduct, 0);
    }

    if (start == 0 && end == list->length)
    {
        return list;
    }

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_LIST;

    reduct_list_t* newList = &item->list;
    newList->length = (uint32_t)len;

    uint8_t flags = atomic_load(&list->flags);

    if (flags & REDUCT_LIST_FLAG_LARGE)
    {
        uint8_t sliceFlags = REDUCT_LIST_FLAG_LARGE;
        if (start > 0)
        {
            sliceFlags |= REDUCT_LIST_FLAG_USED_HEAD;
        }
        else
        {
            sliceFlags |= (flags & REDUCT_LIST_FLAG_USED_HEAD);
        }

        if (end < list->length)
        {
            sliceFlags |= REDUCT_LIST_FLAG_USED_TAIL;
        }
        else
        {
            sliceFlags |= (flags & REDUCT_LIST_FLAG_USED_TAIL);
        }

        atomic_init(&newList->flags, sliceFlags);
        newList->handles = list->handles + start;
        newList->offset = list->offset + (uint32_t)start;
        newList->capacity = list->capacity;
        newList->arena = list->arena;
    }
    else
    {
        atomic_init(&newList->flags, REDUCT_LIST_FLAG_NONE);
        newList->handles = newList->smallHandles;
        newList->offset = 0;
        newList->capacity = REDUCT_LIST_SMALL_MAX;
        memcpy(newList->handles, list->handles + start, len * sizeof(reduct_handle_t));
    }

    return newList;
}

REDUCT_API reduct_list_t* reduct_list_append(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    uint32_t newLen = list->length + 1;

    if (newLen <= REDUCT_LIST_SMALL_MAX)
    {
        reduct_item_t* item = reduct_item_new(reduct);
        item->type = REDUCT_ITEM_TYPE_LIST;
        reduct_list_t* newList = &item->list;
        newList->length = newLen;
        newList->capacity = REDUCT_LIST_SMALL_MAX;
        newList->offset = 0;
        atomic_init(&newList->flags, REDUCT_LIST_FLAG_NONE);
        newList->handles = newList->smallHandles;
        if (list->length > 0)
        {
            memcpy(newList->handles, list->handles, list->length * sizeof(reduct_handle_t));
        }
        newList->handles[list->length] = val;
        return newList;
    }

    uint8_t flags = atomic_load(&list->flags);
    if ((flags & REDUCT_LIST_FLAG_LARGE) && !(flags & REDUCT_LIST_FLAG_USED_TAIL))
    {
        if (list->offset + list->length < list->capacity)
        {
            uint8_t oldFlags = atomic_fetch_or(&list->flags, REDUCT_LIST_FLAG_USED_TAIL);
            if (!(oldFlags & REDUCT_LIST_FLAG_USED_TAIL))
            {
                reduct_item_t* item = reduct_item_new(reduct);
                item->type = REDUCT_ITEM_TYPE_LIST;
                reduct_list_t* newList = &item->list;
                newList->length = newLen;
                newList->capacity = list->capacity;
                newList->offset = list->offset;
                atomic_init(&newList->flags, REDUCT_LIST_FLAG_LARGE | (oldFlags & REDUCT_LIST_FLAG_USED_HEAD));
                newList->handles = list->handles;
                newList->arena = list->arena;
                newList->handles[list->length] = val;
                return newList;
            }
        }
    }

    reduct_list_t* newList = reduct_list_new(reduct, newLen);
    if (list->length > 0)
    {
        memcpy(newList->handles, list->handles, list->length * sizeof(reduct_handle_t));
    }
    newList->handles[list->length] = val;
    return newList;
}

REDUCT_API reduct_list_t* reduct_list_prepend(struct reduct* reduct, reduct_list_t* list, reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    uint32_t newLen = list->length + 1;

    uint8_t flags = atomic_load(&list->flags);
    if ((flags & REDUCT_LIST_FLAG_LARGE) && !(flags & REDUCT_LIST_FLAG_USED_HEAD))
    {
        if (list->offset > 0)
        {
            uint8_t oldFlags = atomic_fetch_or(&list->flags, REDUCT_LIST_FLAG_USED_HEAD);
            if (!(oldFlags & REDUCT_LIST_FLAG_USED_HEAD))
            {
                reduct_item_t* item = reduct_item_new(reduct);
                item->type = REDUCT_ITEM_TYPE_LIST;
                reduct_list_t* newList = &item->list;
                newList->length = newLen;
                newList->capacity = list->capacity;
                newList->offset = list->offset - 1;
                atomic_init(&newList->flags, REDUCT_LIST_FLAG_LARGE | (oldFlags & REDUCT_LIST_FLAG_USED_TAIL));
                newList->handles = list->handles - 1;
                newList->arena = list->arena;
                newList->handles[0] = val;
                return newList;
            }
        }
    }

    reduct_list_t* newList = reduct_list_new(reduct, newLen);
    newList->handles[0] = val;
    if (list->length > 0)
    {
        memcpy(newList->handles + 1, list->handles, list->length * sizeof(reduct_handle_t));
    }
    return newList;
}

REDUCT_API reduct_list_t* reduct_list_concat(struct reduct* reduct, reduct_list_t* a, reduct_list_t* b)
{
    assert(reduct != NULL);
    assert(a != NULL);
    assert(b != NULL);

    if (b->length == 0)
    {
        return a;
    }
    if (a->length == 0)
    {
        return b;
    }

    uint32_t newLen = a->length + b->length;

    if (newLen <= REDUCT_LIST_SMALL_MAX)
    {
        reduct_item_t* item = reduct_item_new(reduct);
        item->type = REDUCT_ITEM_TYPE_LIST;
        reduct_list_t* newList = &item->list;
        newList->length = newLen;
        newList->capacity = REDUCT_LIST_SMALL_MAX;
        newList->offset = 0;
        atomic_init(&newList->flags, REDUCT_LIST_FLAG_NONE);
        newList->handles = newList->smallHandles;
        memcpy(newList->handles, a->handles, a->length * sizeof(reduct_handle_t));
        memcpy(newList->handles + a->length, b->handles, b->length * sizeof(reduct_handle_t));
        return newList;
    }

    uint8_t flagsA = atomic_load(&a->flags);
    if ((flagsA & REDUCT_LIST_FLAG_LARGE) && !(flagsA & REDUCT_LIST_FLAG_USED_TAIL))
    {
        if (a->offset + newLen <= a->capacity)
        {
            uint8_t oldFlags = atomic_fetch_or(&a->flags, REDUCT_LIST_FLAG_USED_TAIL);
            if (!(oldFlags & REDUCT_LIST_FLAG_USED_TAIL))
            {
                reduct_item_t* item = reduct_item_new(reduct);
                item->type = REDUCT_ITEM_TYPE_LIST;
                reduct_list_t* newList = &item->list;
                newList->length = newLen;
                newList->capacity = a->capacity;
                newList->offset = a->offset;
                atomic_init(&newList->flags, REDUCT_LIST_FLAG_LARGE | (oldFlags & REDUCT_LIST_FLAG_USED_HEAD));
                newList->handles = a->handles;
                newList->arena = a->arena;
                memcpy(newList->handles + a->length, b->handles, b->length * sizeof(reduct_handle_t));
                return newList;
            }
        }
    }

    uint8_t flagsB = atomic_load(&b->flags);
    if ((flagsB & REDUCT_LIST_FLAG_LARGE) && !(flagsB & REDUCT_LIST_FLAG_USED_HEAD))
    {
        if (b->offset >= a->length)
        {
            uint8_t oldFlags = atomic_fetch_or(&b->flags, REDUCT_LIST_FLAG_USED_HEAD);
            if (!(oldFlags & REDUCT_LIST_FLAG_USED_HEAD))
            {
                reduct_item_t* item = reduct_item_new(reduct);
                item->type = REDUCT_ITEM_TYPE_LIST;
                reduct_list_t* newList = &item->list;
                newList->length = newLen;
                newList->capacity = b->capacity;
                newList->offset = b->offset - a->length;
                atomic_init(&newList->flags, REDUCT_LIST_FLAG_LARGE | (oldFlags & REDUCT_LIST_FLAG_USED_TAIL));
                newList->handles = b->handles - a->length;
                newList->arena = b->arena;
                memcpy(newList->handles, a->handles, a->length * sizeof(reduct_handle_t));
                return newList;
            }
        }
    }

    reduct_list_t* newList = reduct_list_new(reduct, newLen);
    memcpy(newList->handles, a->handles, a->length * sizeof(reduct_handle_t));
    memcpy(newList->handles + a->length, b->handles, b->length * sizeof(reduct_handle_t));
    return newList;
}

REDUCT_API void reduct_list_retain(reduct_t* reduct, reduct_list_t* list)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_item_retain(REDUCT_CONTAINER_OF(list, reduct_item_t, list));
}

REDUCT_API void reduct_list_release(reduct_t* reduct, reduct_list_t* list)
{
    assert(reduct != NULL);
    assert(list != NULL);

    reduct_item_release(REDUCT_CONTAINER_OF(list, reduct_item_t, list));
}
