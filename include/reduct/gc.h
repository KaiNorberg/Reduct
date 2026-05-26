#ifndef REDUCT_GC_H
#define REDUCT_GC_H 1

#include <reduct/defs.h>

#include <reduct/core.h>
#include <reduct/handle.h>
#include <reduct/item.h>

/**
 * @file gc.h
 * @brief Garbage collection
 * @defgroup gc Garbage Collection
 *
 * @{
 */

#define REDUCT_GC_RETAINED_INITAL 16 ///< Initial capacity for the retained items array.
#define REDUCT_GC_RETAINED_GROWTH 2  ///< Growth factor for the retained items array.

/**
 * @brief Run the garbage collector.
 *
 * @note Usually the garbage collector will only be ran in between the evaluation of two instructions. As such, when
 * evaluation is not taking place, or when evaluating an instruction, there is no need to retain specific items.
 *
 * @param reduct Pointer to the Reduct structure.
 */
REDUCT_API void reduct_gc(reduct_t* reduct);

/**
 * @brief Optionally run the garbage collector if the free list is low.
 *
 * @param reduct Pointer to the Reduct structure.
 */
static inline REDUCT_ALWAYS_INLINE void reduct_gc_if_needed(reduct_t* reduct)
{
    assert(reduct != NULL);

    if (REDUCT_UNLIKELY(reduct->blockCount * REDUCT_ITEM_BLOCK_MAX * 3 > reduct->freeCount * 4 &&
            reduct->blockCount > reduct->prevBlockCount))
    {
        reduct_gc(reduct);
    }
}

/**
 * @brief Retain an item, preventing it from being collected by the garbage collector.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to retain.
 */
REDUCT_API void reduct_gc_retain(reduct_t* reduct, struct reduct_item* item);

/**
 * @brief Release an item, potentially allowing the garbage collector to collect it.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item to release.
 */
REDUCT_API void reduct_gc_release(reduct_t* reduct, struct reduct_item* item);

/** @} */

#endif
