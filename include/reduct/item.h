#ifndef REDUCT_ITEM_H
#define REDUCT_ITEM_H 1

#include <reduct/atom.h>
#include <reduct/closure.h>
#include <reduct/defs.h>
#include <reduct/function.h>
#include <reduct/future.h>
#include <reduct/list.h>
#include <reduct/rvsdg.h>
#include <reduct/task.h>

/**
 * @file item.h
 * @brief Item management.
 * @defgroup item Item
 *
 * An item is a generic container for all data types and heap allocated structures within Reduct.
 *
 * To optimize memory cacheing and reduce fragmentation, all items are 64 bytes and aligned to cache lines.
 *
 * @{
 */

/**
 * @brief Item type enumeration.
 */
typedef uint8_t reduct_item_type_t;
#define REDUCT_ITEM_TYPE_NONE 0          ///< No type.
#define REDUCT_ITEM_TYPE_ATOM 1          ///< An atom.
#define REDUCT_ITEM_TYPE_ATOM_STACK 2    ///< An atom stack.
#define REDUCT_ITEM_TYPE_LIST 3          ///< A list.
#define REDUCT_ITEM_TYPE_LIST_NODE 4     ///< A list node.
#define REDUCT_ITEM_TYPE_FUNCTION 5      ///< A function.
#define REDUCT_ITEM_TYPE_CLOSURE 6       ///< A closure.
#define REDUCT_ITEM_TYPE_RVSDG_NODE 7    ///< An IR node.
#define REDUCT_ITEM_TYPE_RVSDG_EDGE 8    ///< An IR edge.
#define REDUCT_ITEM_TYPE_RVSDG_REGION 9  ///< An IR region.
#define REDUCT_ITEM_TYPE_RVSDG_USER 10   ///< An IR user (input/result).
#define REDUCT_ITEM_TYPE_RVSDG_ORIGIN 11 ///< An IR origin (output/argument).
#define REDUCT_ITEM_TYPE_FUTURE 12       ///< A future.

typedef uint8_t reduct_item_flags_t;       ///< Item flags enumeration.
#define REDUCT_ITEM_FLAG_NONE 0            ///< No flags.
#define REDUCT_ITEM_FLAG_MARKED (1 << 0)   ///< Item is marked by the GC.
#define REDUCT_ITEM_FLAG_RETAINED (1 << 1) ///< Item is retained and should be considered a root by the GC.

#define REDUCT_ITEM_PAYLOAD_MAX 56 ///< The maximum size of the item payload.

/**
 * @brief Item structure.
 * @struct reduct_item_t
 *
 * Should be exactly 64 bytes for caching.
 *
 * @see handle
 */
typedef struct reduct_item
{
    uint32_t position;                  ///< The position in the input buffer where the item was parsed.
    _Atomic(reduct_item_flags_t) flags; ///< Flags for the item.
    reduct_item_type_t type;            ///< The type of the item.
    reduct_input_id_t inputId;          ///< The input ID of the item.
    union {
        uint32_t length;                   ///< Common length for the item. (Stored in the union due to padding rules.)
        reduct_atom_t atom;                ///< An atom.
        reduct_atom_stack_t atomStack;     ///< An atom stack.
        reduct_list_t list;                ///< A list.
        reduct_list_node_t listNode;       ///< A list node.
        reduct_function_t function;        ///< A function.
        reduct_closure_t closure;          ///< A closure.
        reduct_rvsdg_node_t rvsdgNode;     ///< An ir node.
        reduct_rvsdg_edge_t rvsdgEdge;     ///< An ir edge.
        reduct_rvsdg_region_t rvsdgRegion; ///< An ir region.
        reduct_rvsdg_user_t rvsdgUser;     ///< An ir user.
        reduct_rvsdg_origin_t rvsdgOrigin; ///< An ir origin.
        reduct_future_t future;            ///< A future.
        struct reduct_item* free;          ///< The next free item in the free list.
        uint8_t _raw[REDUCT_ITEM_PAYLOAD_MAX];
    };
} reduct_item_t;

#ifdef _Static_assert
_Static_assert(sizeof(reduct_item_t) == REDUCT_ALIGNMENT, "reduct_item_t must be aligned");
#endif

#define REDUCT_ITEM_BLOCK_MAX 127 ///< The maximum number of items in a block.

/**
 * @brief Item block structure.
 * @struct reduct_item_block_t
 *
 * Should be a power of two size as that should help most memory allocators.
 */
typedef struct reduct_item_block
{
    void* allocated; ///< The actual pointer returned by the memory allocation.
    struct reduct_item_block* next;
    uint8_t _padding[REDUCT_ALIGNMENT - sizeof(void*) - sizeof(struct reduct_item_block*)];
    reduct_item_t items[REDUCT_ITEM_BLOCK_MAX];
} reduct_item_block_t;

#ifdef _Static_assert
_Static_assert((sizeof(reduct_item_block_t) & (sizeof(reduct_item_block_t) - 1)) == 0,
    "reduct_item_block_t must be a power of two");
#endif

/**
 * @brief Global item-related state structure.
 * @struct reduct_item_global_t
 */
typedef struct
{
    size_t prevBlockCount;
    size_t blockCount;
    reduct_item_block_t* block;
    mtx_t mutex;
    struct reduct_item* globalFreeList;
    size_t globalFreeCount;
} reduct_item_global_t;

/**
 * @brief Per-thread item-related state structure.
 * @struct reduct_item_local_t
 */
typedef struct
{
    size_t freeCount;
    reduct_item_t* freeList;
} reduct_item_local_t;

/**
 * @brief Initialize a global item state.
 *
 * @param global Pointer to the global item state to initialize.
 */
REDUCT_API void reduct_item_global_init(reduct_item_global_t* global);

/**
 * @brief Deinitialize a global item state.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param global Pointer to the global item state to deinitialize.
 */
REDUCT_API void reduct_item_global_deinit(struct reduct* reduct, reduct_item_global_t* global);

/**
 * @brief Initialize a local item state.
 *
 * @param local Pointer to the local item state to initialize.
 */
REDUCT_API void reduct_item_local_init(reduct_item_local_t* local);

/**
 * @brief Deinitialize a local item state.
 *
 * @param local Pointer to the local item state to deinitialize.
 */
REDUCT_API void reduct_item_local_deinit(reduct_item_local_t* local);

/**
 * @brief Allocate a new item.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return A pointer to the newly created item.
 */
REDUCT_API reduct_item_t* reduct_item_new(struct reduct* reduct);

/**
 * @brief Deinitialize a item without adding it to the freelist.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to deinitialize.
 */
REDUCT_API void reduct_item_deinit(struct reduct* reduct, reduct_item_t* item);

/**
 * @brief Free an item.
 *
 * Intended to be used by the GC or when it is known that an item has no more users and was allocated by the current
 * thread.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to free.
 */
REDUCT_API void reduct_item_free(struct reduct* reduct, reduct_item_t* item);

/**
 * @brief Get the string representation of the type of an item.
 *
 * @param item Pointer to the item.
 * @return The string representation of the item type.
 */
REDUCT_API const char* reduct_item_type_str(reduct_item_t* item);

/**
 * @brief Mark an item as reachable.
 *
 * Used by the garbage collector.
 *
 * @param item Pointer to the item to mark.
 */
REDUCT_API void reduct_item_mark(reduct_item_t* item);

/**
 * @brief Retain an item, preventing it from being collected by the garbage collector.
 *
 * @param item Pointer to the item to retain.
 */
static inline REDUCT_ALWAYS_INLINE void reduct_item_retain(struct reduct_item* item)
{
    assert(item != NULL);
    atomic_fetch_or(&item->flags, REDUCT_ITEM_FLAG_RETAINED);
}

/**
 * @brief Release an item, potentially allowing the garbage collector to collect it.
 *
 * @param item Pointer to the item to release.
 */
static inline REDUCT_ALWAYS_INLINE void reduct_item_release(struct reduct_item* item)
{
    assert(item != NULL);
    atomic_fetch_and(&item->flags, ~REDUCT_ITEM_FLAG_RETAINED);
}

/** @} */

#endif
