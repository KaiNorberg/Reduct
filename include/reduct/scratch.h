#ifndef REDUCT_SCRATCH_H
#define REDUCT_SCRATCH_H 1

#include <reduct/atom.h>
#include <reduct/defs.h>
#include <reduct/error.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/native.h>
#include <reduct/schema.h>

/**
 * @file scratch.h
 * @brief Scratch buffer allocation
 * @defgroup Scratch
 *
 * @{
 */

#define REDUCT_SCRATCH_INITIAL 128 ///< Initial scratch buffer size.
#define REDUCT_SCRATCH_MAX 16      ///< The maximum number of scratch buffers.

/**
 * @brief Scratch buffer structure.
 * @struct reduct_scratch_t
 */
typedef struct reduct_scratch
{
    char* buffer;
    size_t length;
} reduct_scratch_t;

/**
 * @brief Per-thread scratch-related state structure.
 * @struct reduct_scratch_state_t
 */
typedef struct reduct_scratch_state
{
    size_t size;
    reduct_scratch_t buffers[REDUCT_SCRATCH_MAX];
} reduct_scratch_state_t;

/**
 * @brief Initialize a scratch state.
 *
 * @param state Pointer to the scratch state to initialize.
 */
REDUCT_API void reduct_scratch_state_init(reduct_scratch_state_t* state);

/**
 * @brief Deinitialize a scratch state.
 *
 * @param state Pointer to the scratch state to deinitialize.
 */
REDUCT_API void reduct_scratch_state_deinit(reduct_scratch_state_t* state);

/**
 * @brief Allocate a scratch buffer.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _name The name of the buffer pointer.
 * @param _type The type of the elements.
 * @param _length The number of elements of `_type` to reserve memory for.
 */
#define REDUCT_SCRATCH_GET(_reduct, _name, _type, _length) \
    _type* _name = NULL; \
    do \
    { \
        size_t _needed = (_length) * sizeof(_type); \
        if ((_reduct)->scratch.size >= REDUCT_SCRATCH_MAX) \
        { \
            REDUCT_ERROR_INTERNAL(_reduct, "scratch buffer overflow"); \
        } \
        reduct_scratch_t* _s = &(_reduct)->scratch.buffers[(_reduct)->scratch.size++]; \
        _s->buffer = malloc(_needed); \
        if (_s->buffer == NULL) \
        { \
            REDUCT_ERROR_INTERNAL(_reduct, "out of memory"); \
        } \
        _s->length = _needed; \
        _name = (_type*)_s->buffer; \
    } while (0)

/**
 * @brief Grow an allocated scratch buffer, the current buffer must be the last one allocated.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _name The name of the buffer pointer.
 * @param _type The type of the elements.
 * @param _length The number of elements of `_type` to reserve memory for.
 */
#define REDUCT_SCRATCH_GROW(_reduct, _name, _type, _length) \
    do \
    { \
        size_t _needed = (_length) * sizeof(_type); \
        reduct_scratch_t* _s = &(_reduct)->scratch.buffers[(_reduct)->scratch.size - 1]; \
        _s->buffer = realloc(_s->buffer, _needed); \
        if (_s->buffer == NULL) \
        { \
            REDUCT_ERROR_INTERNAL(_reduct, "out of memory"); \
        } \
        _s->length = _needed; \
        _name = (_type*)_s->buffer; \
    } while (0)

/**
 * @brief Free a scratch buffer, the current buffer must be the last one allocated.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _name The name of the buffer pointer.
 */
#define REDUCT_SCRATCH_PUT(_reduct, _name) \
    do \
    { \
        assert((_reduct)->scratch.size > 0); \
        reduct_scratch_t* _s = &(_reduct)->scratch.buffers[--(_reduct)->scratch.size]; \
        free(_s->buffer); \
        _s->buffer = NULL; \
        _s->length = 0; \
        _name = NULL; \
    } while (0)

/** @} */

#endif
