#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/native.h>

REDUCT_API void reduct_native_global_init(reduct_native_global_t* global)
{
    assert(global != NULL);
    global->map = NULL;
    global->size = 0;
    global->capacity = 0;
    global->mask = 0;
    reduct_rwmutex_init(&global->mutex);
}

REDUCT_API void reduct_native_global_deinit(reduct_native_global_t* global)
{
    assert(global != NULL);
    if (global->map != NULL)
    {
        for (size_t i = 0; i < global->capacity; i++)
        {
            if (global->map[i].name != NULL)
            {
                free(global->map[i].name);
            }
        }
        free(global->map);
    }
    reduct_rwmutex_destroy(&global->mutex);
}

static inline reduct_native_entry_t* reduct_native_map_find_unlocked(reduct_t* reduct, uint32_t hash, const char* str,
    size_t len)
{
    reduct_native_global_t* global = &reduct->global->native;
    if (global->map == NULL)
    {
        return NULL;
    }

    size_t index = hash & global->mask;
    size_t step = 1;

    while (global->map[index].name != NULL)
    {
        reduct_native_entry_t* entry = &global->map[index];
        if (entry->hash == hash && entry->length == len && memcmp(entry->name, str, len) == 0)
        {
            return entry;
        }
        index = (index + step) & global->mask;
        step++;
    }

    return NULL;
}

static inline bool reduct_native_map_grow(reduct_t* reduct)
{
    reduct_native_global_t* global = &reduct->global->native;
    size_t oldCapacity = global->capacity;
    reduct_native_entry_t* oldMap = global->map;

    global->capacity = oldCapacity * REDUCT_NATIVE_MAP_GROWTH;
    global->mask = global->capacity - 1;
    global->map = calloc(global->capacity, sizeof(reduct_native_entry_t));
    if (global->map == NULL)
    {
        global->capacity = oldCapacity;
        global->mask = oldCapacity - 1;
        global->map = oldMap;
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    for (size_t i = 0; i < oldCapacity; i++)
    {
        if (oldMap[i].name == NULL)
        {
            continue;
        }

        size_t index = oldMap[i].hash & global->mask;
        size_t step = 1;
        while (global->map[index].name != NULL)
        {
            index = (index + step) & global->mask;
            step++;
        }
        global->map[index] = oldMap[i];
    }

    if (oldMap != NULL)
    {
        free(oldMap);
    }
    return true;
}

static inline reduct_native_entry_t* reduct_native_map_insert(reduct_t* reduct, uint32_t hash, const char* name,
    size_t len)
{
    reduct_native_global_t* env = &reduct->global->native;
    if (env->map == NULL)
    {
        env->capacity = REDUCT_NATIVE_MAP_INITIAL;
        env->mask = env->capacity - 1;
        env->size = 0;
        env->map = calloc(env->capacity, sizeof(reduct_native_entry_t));
        if (env->map == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    if (env->size * 4 > env->capacity * 3)
    {
        reduct_native_map_grow(reduct);
    }

    size_t index = hash & env->mask;
    size_t step = 1;
    while (env->map[index].name != NULL)
    {
        index = (index + step) & env->mask;
        step++;
    }

    reduct_native_entry_t* entry = &env->map[index];
    entry->hash = hash;
    entry->length = len;
    entry->name = malloc(len + 1);
    if (entry->name == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memcpy(entry->name, name, len);
    entry->name[len] = '\0';

    env->size++;
    return entry;
}

REDUCT_API reduct_native_entry_t* reduct_native_map_find(reduct_t* reduct, uint32_t hash, const char* str, size_t len)
{
    assert(reduct != NULL);

    reduct_rwmutex_read_lock(&reduct->global->native.mutex);
    reduct_native_entry_t* entry = reduct_native_map_find_unlocked(reduct, hash, str, len);
    reduct_rwmutex_read_unlock(&reduct->global->native.mutex);
    return entry;
}

REDUCT_API void reduct_native_register(reduct_t* reduct, const reduct_native_t* array, size_t count)
{
    assert(reduct != NULL);
    assert(array != NULL || count == 0);

    reduct_rwmutex_write_lock(&reduct->global->native.mutex);

    for (size_t i = 0; i < count; i++)
    {
        const reduct_native_t* native = &array[i];
        size_t len = strlen(native->name);
        uint32_t hash = reduct_hash(native->name, len);

        reduct_native_entry_t* entry = reduct_native_map_find_unlocked(reduct, hash, native->name, len);
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

    reduct_rwmutex_write_unlock(&reduct->global->native.mutex);
}
