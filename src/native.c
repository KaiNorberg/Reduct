#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/intrinsic.h>
#include <reduct/native.h>

REDUCT_API void reduct_native_env_init(reduct_native_env_t* env)
{
    assert(env != NULL);
    env->map = NULL;
    env->size = 0;
    env->capacity = 0;
    env->mask = 0;
    reduct_rwmutex_init(&env->mutex);
}

REDUCT_API void reduct_native_env_deinit(reduct_native_env_t* env)
{
    assert(env != NULL);
    if (env->map != NULL)
    {
        for (size_t i = 0; i < env->capacity; i++)
        {
            if (env->map[i].name != NULL)
            {
                free(env->map[i].name);
            }
        }
        free(env->map);
    }
    reduct_rwmutex_destroy(&env->mutex);
}

static inline reduct_native_entry_t* reduct_native_map_find_unlocked(reduct_t* reduct, uint32_t hash, const char* str,
    size_t len)
{
    reduct_native_env_t* env = &reduct->env->native;
    if (env->map == NULL)
    {
        return NULL;
    }

    size_t index = hash & env->mask;
    size_t step = 1;

    while (env->map[index].name != NULL)
    {
        reduct_native_entry_t* entry = &env->map[index];
        if (entry->hash == hash && entry->length == len && memcmp(entry->name, str, len) == 0)
        {
            return entry;
        }
        index = (index + step) & env->mask;
        step++;
    }

    return NULL;
}

static inline bool reduct_native_map_grow(reduct_t* reduct)
{
    reduct_native_env_t* env = &reduct->env->native;
    size_t oldCapacity = env->capacity;
    reduct_native_entry_t* oldMap = env->map;

    env->capacity = oldCapacity * REDUCT_NATIVE_MAP_GROWTH;
    env->mask = env->capacity - 1;
    env->map = calloc(env->capacity, sizeof(reduct_native_entry_t));
    if (env->map == NULL)
    {
        env->capacity = oldCapacity;
        env->mask = oldCapacity - 1;
        env->map = oldMap;
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    for (size_t i = 0; i < oldCapacity; i++)
    {
        if (oldMap[i].name == NULL)
        {
            continue;
        }

        size_t index = oldMap[i].hash & env->mask;
        size_t step = 1;
        while (env->map[index].name != NULL)
        {
            index = (index + step) & env->mask;
            step++;
        }
        env->map[index] = oldMap[i];
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
    reduct_native_env_t* env = &reduct->env->native;
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

    reduct_rwmutex_read_lock(&reduct->env->native.mutex);
    reduct_native_entry_t* entry = reduct_native_map_find_unlocked(reduct, hash, str, len);
    reduct_rwmutex_read_unlock(&reduct->env->native.mutex);
    return entry;
}

REDUCT_API void reduct_native_register(reduct_t* reduct, const reduct_native_t* array, size_t count)
{
    assert(reduct != NULL);
    assert(array != NULL || count == 0);

    reduct_rwmutex_write_lock(&reduct->env->native.mutex);

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

    reduct_rwmutex_write_unlock(&reduct->env->native.mutex);
}
