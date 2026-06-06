#include "reduct/sync.h"
#ifndef REDUCT_GC_H
#define REDUCT_GC_H 1

#include <reduct/defs.h>

#include <reduct/handle.h>
#include <reduct/item.h>

/**
 * @file gc.h
 * @brief Garbage collection
 * @defgroup gc Garbage Collection
 *
 * @todo Reimplement Garbage Collector.
 *
 * @{
 */

#define REDUCT_GC_RETAINED_INITIAL 16 ///< Initial capacity for the retained items array.
#define REDUCT_GC_RETAINED_GROWTH 2   ///< Growth factor for the retained items array.

/**
 * @brief Per-thread garbage collection-related state structure.
 * @struct reduct_gc_state_t
 */
typedef struct
{
    reduct_item_t** retained; ///< Array of additional items to be considered roots during GC.
    size_t retainedCount;     ///< Number of items currently retained.
    size_t retainedCapacity;  ///< Capacity of the retained items array.
    uint32_t lastCount;       ///< The last GC count this thread observed.
} reduct_gc_state_t;

/**
 * @brief Global garbage collection-related environment structure.
 * @struct reduct_gc_env_t
 */
typedef struct
{
    mtx_t mutex;
    _Atomic(uint32_t) count;
    uint32_t completed;
} reduct_gc_env_t;

/**
 * @brief Initialize an gc environment.
 *
 * @param env Pointer to the gc environment to initialize.
 */
REDUCT_API void reduct_gc_env_init(reduct_gc_env_t* env);

/**
 * @brief Deinitialize an gc environment.
 *
 * @param env Pointer to the gc environment to deinitialize.
 */
REDUCT_API void reduct_gc_env_deinit(reduct_gc_env_t* env);

/**
 * @brief Initialize a gc state.
 *
 * @param state Pointer to the gc state to initialize.
 */
REDUCT_API void reduct_gc_state_init(reduct_gc_state_t* state);

/**
 * @brief Deinitialize a gc state.
 *
 * @param state Pointer to the gc state to deinitialize.
 */
REDUCT_API void reduct_gc_state_deinit(reduct_gc_state_t* state);

/**
 * @brief Run the garbage collector.
 *
 * @note Usually the garbage collector will only be ran in between the evaluation of two instructions. As such, when
 * evaluation is not taking place, or when evaluating an instruction, there is no need to retain specific items.
 *
 * @param reduct Pointer to the Reduct structure.
 */
REDUCT_API void reduct_gc(struct reduct* reduct);

/**
 * @brief Check if the garbage collector should be ran and run it if so.
 *
 * State and env pointer must be specified sep
 *
 * @param _reduct Pointer to the Reduct structure.
 */
#define REDUCT_GC_CHECK(_reduct) \
    do \
    { \
        if (REDUCT_UNLIKELY( \
                atomic_load_explicit(&(_reduct)->env->gc.count, memory_order_acquire) > (_reduct)->gc.lastCount)) \
        { \
            reduct_gc(_reduct); \
        } \
    } while (0)

/**
 * @brief Request that the garbage collector be ran without immediately running it.
 *
 * @param gc Pointer to the GC environment.
 */
static inline REDUCT_ALWAYS_INLINE void reduct_gc_request(reduct_gc_env_t* gc)
{
    atomic_fetch_add_explicit(&gc->count, 1, memory_order_release);
}

/**
 * @brief Retain an item, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to retain.
 */
REDUCT_API void reduct_gc_retain(struct reduct* reduct, struct reduct_item* item);

/**
 * @brief Release an item, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to release.
 */
REDUCT_API void reduct_gc_release(struct reduct* reduct, struct reduct_item* item);

/** @} */

#endif
