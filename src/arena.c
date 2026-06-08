#include <reduct/arena.h>
#include <reduct/core.h>

#include <assert.h>
#include <string.h>

REDUCT_API void reduct_arena_local_init(reduct_arena_local_t* local)
{
    assert(local != NULL);
    local->current = NULL;
}

REDUCT_API void reduct_arena_local_deinit(reduct_arena_local_t* local)
{
    assert(local != NULL);
    local->current = NULL;
}

static inline reduct_arena_t* reduct_arena_new(reduct_t* reduct, size_t capacity)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_ARENA;

    reduct_arena_t* arena = &item->arena;
    arena->capacity = capacity;
    arena->count = 0;
    arena->data = calloc(1, capacity);
    if (arena->data == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    return arena;
}

static inline reduct_arena_t* reduct_arena_get(reduct_t* reduct, size_t size)
{
    reduct_arena_t* arena = reduct->arena.current;
    size_t offset = (arena != NULL) ? REDUCT_ROUND_UP(arena->count, 8) : 0;
    if (arena == NULL || offset + size > arena->capacity)
    {
        size_t capacity = REDUCT_ARENA_MIN;
        while (capacity < size)
        {
            capacity *= REDUCT_ARENA_GROWTH;
        }
        arena = reduct_arena_new(reduct, capacity);

        arena->next = reduct->arena.current;
        reduct->arena.current = arena;
        if (arena->next != NULL)
        {
            arena->next->prev = arena;
        }
        arena->prev = NULL;
    }

    return arena;
}

REDUCT_API void reduct_arena_alloc(struct reduct* reduct, size_t size, reduct_arena_chunk_t* out)
{
    assert(reduct != NULL);
    assert(out != NULL);
    assert(size > 0);

    reduct_arena_t* arena = reduct_arena_get(reduct, size);
    size_t offset = REDUCT_ROUND_UP(arena->count, 8);

    out->arena = arena;
    out->data = (uint8_t*)arena->data + offset;
    out->size = size;
    arena->count = offset + size;
}

REDUCT_API void reduct_arena_alloc_super(struct reduct* reduct, size_t size, reduct_arena_chunk_t* chunk,
    reduct_arena_chunk_t* out)
{
    assert(reduct != NULL);
    assert(chunk != NULL);
    assert(out != NULL);

    if (chunk->arena != NULL)
    {
        reduct_arena_t* arena = chunk->arena;
        uint8_t* chunkEnd = (uint8_t*)chunk->data + chunk->size;
        uint8_t* arenaEnd = (uint8_t*)arena->data + arena->count;
        if (chunkEnd == arenaEnd && arena->count + size - chunk->size <= arena->capacity)
        {
            arena->count += size - chunk->size;
            out->arena = arena;
            out->data = chunk->data;
            out->size = size;
            return;
        }
    }

    reduct_arena_alloc(reduct, size, out);
    if (chunk->data != NULL && chunk->size > 0)
    {
        memcpy(out->data, chunk->data, chunk->size);
    }
}