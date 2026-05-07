#include "reduct/atom.h"
#include "reduct/core.h"
#include "reduct/intrinsic.h"
#include "reduct/native.h"

static inline reduct_bool_t reduct_native_map_grow(reduct_t* reduct)
{
    size_t oldCapacity = reduct->nativeMapCapacity;
    reduct_native_entry_t* oldMap = reduct->nativeMap;

    reduct->nativeMapCapacity = oldCapacity * REDUCT_NATIVE_MAP_GROWTH;
    reduct->nativeMapMask = reduct->nativeMapCapacity - 1;
    reduct->nativeMap = calloc(reduct->nativeMapCapacity, sizeof(reduct_native_entry_t));
    if (reduct->nativeMap == NULL)
    {
        reduct->nativeMapCapacity = oldCapacity;
        reduct->nativeMapMask = oldCapacity - 1;
        reduct->nativeMap = oldMap;
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    for (size_t i = 0; i < oldCapacity; i++)
    {
        if (oldMap[i].name == NULL)
        {
            continue;
        }

        size_t index = oldMap[i].hash & reduct->nativeMapMask;
        size_t step = 1;
        while (reduct->nativeMap[index].name != NULL)
        {
            index = (index + step) & reduct->nativeMapMask;
            step++;
        }
        reduct->nativeMap[index] = oldMap[i];
    }

    if (oldMap != NULL)
    {
        free(oldMap);
    }
    return REDUCT_TRUE;
}

static inline reduct_native_entry_t* reduct_native_map_insert(reduct_t* reduct, uint32_t hash,
    const char* name, size_t len)
{
    if (reduct->nativeMap == NULL)
    {
        reduct->nativeMapCapacity = REDUCT_NATIVE_MAP_INITIAL;
        reduct->nativeMapMask = reduct->nativeMapCapacity - 1;
        reduct->nativeMapSize = 0;
        reduct->nativeMap = calloc(reduct->nativeMapCapacity, sizeof(reduct_native_entry_t));
        if (reduct->nativeMap == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    if (reduct->nativeMapSize * 4 > reduct->nativeMapCapacity * 3)
    {
        reduct_native_map_grow(reduct);
    }

    size_t index = hash & reduct->nativeMapMask;
    size_t step = 1;
    while (reduct->nativeMap[index].name != NULL)
    {
        index = (index + step) & reduct->nativeMapMask;
        step++;
    }

    reduct_native_entry_t* entry = &reduct->nativeMap[index];
    entry->hash = hash;
    entry->length = len;
    entry->name = malloc(len + 1);
    if (entry->name == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memcpy(entry->name, name, len);
    entry->name[len] = '\0';

    reduct->nativeMapSize++;
    return entry;
}

REDUCT_API reduct_native_entry_t* reduct_native_map_find(reduct_t* reduct, uint32_t hash,
    const char* str, size_t len)
{
    if (reduct->nativeMap == NULL)
    {
        return NULL;
    }

    size_t index = hash & reduct->nativeMapMask;
    size_t step = 1;

    while (reduct->nativeMap[index].name != NULL)
    {
        reduct_native_entry_t* entry = &reduct->nativeMap[index];
        if (entry->hash == hash && entry->length == len && memcmp(entry->name, str, len) == 0)
        {
            return entry;
        }
        index = (index + step) & reduct->nativeMapMask;
        step++;
    }

    return NULL;
}

REDUCT_API void reduct_native_register(reduct_t* reduct, reduct_native_t* array, size_t count)
{
    assert(reduct != NULL);
    assert(array != NULL || count == 0);

    for (size_t i = 0; i < count; i++)
    {
        reduct_native_t* native = &array[i];
        size_t len = strlen(native->name);
        uint32_t hash = reduct_hash(native->name, len);

        reduct_native_entry_t* entry = reduct_native_map_find(reduct, hash, native->name, len);
        if (entry != NULL)
        {
            entry->nativeFn = native->nativeFn;
            entry->intrinsicFn = native->intrinsicFn;
            continue;
        }

        entry = reduct_native_map_insert(reduct, hash, native->name, len);
        entry->nativeFn = native->nativeFn;
        entry->intrinsicFn = native->intrinsicFn;
    }
}