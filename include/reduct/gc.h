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

/**
 * @brief Global garbage collection-related state structure.
 * @struct reduct_gc_global_t
 */
typedef struct
{
    _Atomic(bool) requested;
} reduct_gc_global_t;

/**
 * @brief Initialize a global gc state.
 *
 * @param global Pointer to the global gc state to initialize.
 */
REDUCT_API void reduct_gc_global_init(reduct_gc_global_t* global);

/**
 * @brief Deinitialize a global gc state.
 *
 * @param global Pointer to the global gc state to deinitialize.
 */
REDUCT_API void reduct_gc_global_deinit(reduct_gc_global_t* global);

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
        if (REDUCT_UNLIKELY(atomic_load_explicit(&_reduct->global->gc.requested, memory_order_relaxed))) \
        { \
            reduct_gc(_reduct); \
        } \
    } while (0)

/**
 * @brief Request that the garbage collector be ran without immediately running it.
 *
 * @param gc Pointer to the GC environment.
 */
static inline REDUCT_ALWAYS_INLINE void reduct_gc_request(reduct_gc_global_t* gc)
{
    atomic_store_explicit(&gc->requested, true, memory_order_relaxed);
}

/** @} */

#endif
