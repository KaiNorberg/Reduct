#ifndef REDUCT_SYNC_H
#define REDUCT_SYNC_H 1

#include <reduct/defs.h>

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/**
 * @file sync.h
 * @brief Syncronization primitives.
 * @defgroup sync Sync
 *
 * @{
 */

/**
 * @brief Read-Write Mutex structure.
 * @struct reduct_rwmutex_t
 */
typedef struct
{
    mtx_t mtx;
    cnd_t cnd;
    uint32_t readers;
    uint32_t writersWaiting;
    bool writing;
} reduct_rwmutex_t;

/**
 * @brief Initialize a read-write mutex.
 *
 * @param rw Pointer to the rwmutex to initialize.
 */
static inline void reduct_rwmutex_init(reduct_rwmutex_t* rw)
{
    mtx_init(&rw->mtx, mtx_plain);
    cnd_init(&rw->cnd);
    rw->readers = 0;
    rw->writersWaiting = 0;
    rw->writing = false;
}

/**
 * @brief Destroy a read-write mutex.
 *
 * @param rw Pointer to the rwmutex to destroy.
 */
static inline void reduct_rwmutex_destroy(reduct_rwmutex_t* rw)
{
    mtx_destroy(&rw->mtx);
    cnd_destroy(&rw->cnd);
}

/**
 * @brief Lock a rwmutex for reading.
 *
 * @param rw Pointer to the rwmutex to lock.
 */
static inline void reduct_rwmutex_read_lock(reduct_rwmutex_t* rw)
{
    mtx_lock(&rw->mtx);
    while (rw->writing || rw->writersWaiting > 0)
    {
        cnd_wait(&rw->cnd, &rw->mtx);
    }
    rw->readers++;
    mtx_unlock(&rw->mtx);
}

/**
 * @brief Unlock a rwmutex after reading.
 *
 * @param rw Pointer to the rwmutex to unlock.
 */
static inline void reduct_rwmutex_read_unlock(reduct_rwmutex_t* rw)
{
    mtx_lock(&rw->mtx);
    rw->readers--;
    if (rw->readers == 0)
    {
        cnd_broadcast(&rw->cnd);
    }
    mtx_unlock(&rw->mtx);
}

/**
 * @brief Lock a rwmutex for writing.
 *
 * @param rw Pointer to the rwmutex to lock.
 */
static inline void reduct_rwmutex_write_lock(reduct_rwmutex_t* rw)
{
    mtx_lock(&rw->mtx);
    rw->writersWaiting++;
    while (rw->writing || rw->readers > 0)
    {
        cnd_wait(&rw->cnd, &rw->mtx);
    }
    rw->writersWaiting--;
    rw->writing = true;
    mtx_unlock(&rw->mtx);
}

/**
 * @brief Unlock a rwmutex after writing.
 *
 * @param rw Pointer to the rwmutex to unlock.
 */
static inline void reduct_rwmutex_write_unlock(reduct_rwmutex_t* rw)
{
    mtx_lock(&rw->mtx);
    rw->writing = false;
    cnd_broadcast(&rw->cnd);
    mtx_unlock(&rw->mtx);
}

/** @} */

#endif
