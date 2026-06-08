#ifndef REDUCT_ARENA_H
#define REDUCT_ARENA_H 1

#include <reduct/defs.h>

#include <stddef.h>
#include <stdint.h>

/**
 * @file arena.h
 * @brief Arena allocation.
 * @defgroup arena Arena
 *
 * @{
 */

#define REDUCT_ARENA_MIN 1024 ///< The minimum size of an arena in bytes.
#define REDUCT_ARENA_GROWTH \
    2 ///< The factor by which we increase the minimum size until the needed capacity is reached.

/**
 * @brief Arena structure.
 * @struct reduct_arena_t
 */
typedef struct reduct_arena
{
    struct reduct_arena* next;
    struct reduct_arena* prev;
    size_t capacity;
    size_t count;
    void* data;
} reduct_arena_t;

/**
 * @brief Arena chunk descriptor.
 * @struct reduct_arena_chunk_t
 */
typedef struct
{
    reduct_arena_t* arena;
    size_t size;
    void* data;
} reduct_arena_chunk_t;

/**
 * @brief Create an arena chunk descriptor.
 *
 * @param arena Pointer to the arena the chunk belongs to.
 * @param size The size of the chunk in bytes.
 * @param data Pointer to the chunk data.
 * @return An arena chunk descriptor.
 */
#define REDUCT_ARENA_CHUNK(_arena, _size, _data) ((reduct_arena_chunk_t){(_arena), (_size), (_data)})

/**
 * @brief Per-thread arena-related state structure.
 * @struct reduct_arena_local_t
 */
typedef struct
{
    reduct_arena_t* current;
} reduct_arena_local_t;

/**
 * @brief Initialize a local arena state.
 *
 * @param local Pointer to the local arena state to initialize.
 */
REDUCT_API void reduct_arena_local_init(reduct_arena_local_t* local);

/**
 * @brief Deinitialize a local arena state.
 *
 * @param local Pointer to the local arena state to deinitialize.
 */
REDUCT_API void reduct_arena_local_deinit(reduct_arena_local_t* local);

/**
 * @brief Allocate a chunk of memory from a arena.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param size The size of the chunk to allocate in bytes.
 * @param out Pointer to store the allocated chunk descriptor.
 */
REDUCT_API void reduct_arena_alloc(struct reduct* reduct, size_t size, reduct_arena_chunk_t* out);

/**
 * @brief Allocate a super chunk that starts with the specified chunk.
 *
 * If the chunk is at the end of its arena and there is enough capacity, it will extend the existing allocation.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param size The size of the super chunk to allocate in bytes.
 * @param chunk The description of the chunk to extend from.
 * @param out Pointer to store the allocated super chunk information.
 */
REDUCT_API void reduct_arena_alloc_super(struct reduct* reduct, size_t size, reduct_arena_chunk_t* chunk,
    reduct_arena_chunk_t* out);

/** @} */

#endif