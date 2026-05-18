#ifndef REDUCT_CORE_H
#define REDUCT_CORE_H 1

#include "reduct/atom.h"
#include "reduct/defs.h"
#include "reduct/error.h"
#include "reduct/item.h"
#include "reduct/list.h"
#include "reduct/native.h"
#include "reduct/schema.h"

struct reduct_item;
struct reduct_eval_frame;

#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>

/**
 * @file core.h
 * @brief Core definitions and structures.
 * @defgroup core
 *
 * @{
 */

#define REDUCT_IMPORT_PATHS_INITIAL 4 ///< Initial size of the import path array.
#define REDUCT_IMPORT_PATHS_GROWTH 2  ///< Growth factor of the import path array.

#define REDUCT_SCHEMA_INITIAL 4 ///< Initial size of the schema array.
#define REDUCT_SCHEMA_GROWTH 2  ///< Growth factor of the schema array.

#define REDUCT_CONSTANTS_MAX 8 ///< Maximum amount of predefined constants.

/**
 * @brief Input flags.
 */
typedef enum
{
    REDUCT_INPUT_FLAG_NONE = 0,
    REDUCT_INPUT_FLAG_OWNED = 1 ///< The input buffer is owned by the input structure and should be freed.
} reduct_input_flags_t;

/**
 * @brief Input structure.
 * @struct reduct_input_t
 */
typedef struct reduct_input
{
    struct reduct_input* prev;
    reduct_handle_t ast;
    const char* buffer;
    const char* end;
    reduct_input_id_t id;
    reduct_input_flags_t flags;
    char path[REDUCT_PATH_MAX];
} reduct_input_t;

/**
 * @brief Scratch buffer structure.
 * @struct reduct_scratch_t
 */
typedef struct reduct_scratch
{
    char* buffer;
    size_t length;
} reduct_scratch_t;

#define REDUCT_SCRATCH_INITIAL 128 ///< Initial scratch buffer size.
#define REDUCT_SCRATCH_MAX 16      ///< The maximum number of scratch buffers.

#define REDUCT_LIBS_INITIAL 4 ///< Initial size of the library array.
#define REDUCT_LIBS_GROWTH 2  ///< Growth factor of the library array.

/**
 * @brief State structure.
 * @struct reduct_t
 */
typedef struct reduct
{
    reduct_handle_t nil;
    struct reduct_eval_frame* frames;
    size_t frameCount;
    size_t frameCapacity;
    reduct_handle_t* regs;
    size_t regCount;
    size_t regCapacity;
    reduct_item_t** retained;
    size_t retainedCount;
    size_t retainedCapacity;
    size_t prevBlockCount;
    size_t blockCount;
    reduct_item_block_t* block;
    size_t freeCount;
    reduct_item_t* freeList;
    reduct_atom_stack_t* atomStack;
    size_t atomMapSize;
    size_t atomMapTombstones;
    size_t atomMapCapacity;
    size_t atomMapMask;
    reduct_atom_t** atomMap;
    size_t nativeMapSize;
    size_t nativeMapCapacity;
    size_t nativeMapMask;
    reduct_native_entry_t* nativeMap;
    size_t scratchSize;
    size_t scratchCapacity;
    reduct_scratch_t scratch[REDUCT_SCRATCH_MAX];
    reduct_input_t* input;
    reduct_error_t* error;
    reduct_input_id_t newInputId;
    reduct_lib_t* libs;
    size_t libCount;
    size_t libCapacity;
    char** importPaths;
    size_t importPathCount;
    size_t importPathCapacity;
    reduct_schema_internal_t** schemas;
    size_t schemaCount;
    size_t schemaCapacity;
    void* userdata;
    int argc;
    char** argv;
} reduct_t;

/**
 * @brief Create a new Reduct structure.
 *
 * @param error Pointer to the error structure to be used for error reporting.
 * @return A pointer to the newly allocated Reduct structure.
 */
REDUCT_API reduct_t* reduct_new(reduct_error_t* error);

/**
 * @brief Free the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure to free.
 */
REDUCT_API void reduct_free(reduct_t* reduct);

/**
 * @brief Set the user data pointer for the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param userdata The user data pointer.
 */
REDUCT_API void reduct_userdata_set(reduct_t* reduct, void* userdata);

/**
 * @brief Get the user data pointer from the Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @return The user data pointer.
 */
REDUCT_API void* reduct_userdata_get(reduct_t* reduct);

/**
 * @brief Set the command line arguments for the Reduct structure.
 *
 * Will be utilized by the `(args!)` native.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param argc The number of arguments.
 * @param argv The argument strings.
 */
REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv);

/**
 * @brief Add a path to search when importing modules.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param path The directory path, will be copied.
 */
REDUCT_API void reduct_add_import_path(reduct_t* reduct, const char* path);

/**
 * @brief Create a new input structure and push it onto the input stack.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param buffer The input buffer.
 * @param length The length of the input buffer.
 * @param path The path to the input file.
 * @param flags Input flags.
 * @return A pointer to the newly created input structure.
 */
REDUCT_API reduct_input_t* reduct_input_new(reduct_t* reduct, const char* buffer, size_t length, const char* path,
    reduct_input_flags_t flags);

/**
 * @brief Lookup an input structure by its ID.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the input structure.
 * @return A pointer to the input structure, or `NULL` if not found.
 */
REDUCT_API reduct_input_t* reduct_input_lookup(reduct_t* reduct, reduct_input_id_t id);

/**
 * @brief Allocate a scratch buffer.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _name The name of the buffer pointer.
 * @param _type The type of the elements.
 * @param _length The number of elements of `_type` to reserve memory for.
 */
#define REDUCT_SCRATCH(_reduct, _name, _type, _length) \
    _type* _name = NULL; \
    do \
    { \
        size_t _needed = (_length) * sizeof(_type); \
        if ((_reduct)->scratchSize >= REDUCT_SCRATCH_MAX) \
        { \
            REDUCT_ERROR_INTERNAL(_reduct, "scratch buffer overflow"); \
        } \
        reduct_scratch_t* _s = &(_reduct)->scratch[(_reduct)->scratchSize++]; \
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
        reduct_scratch_t* _s = &(_reduct)->scratch[(_reduct)->scratchSize - 1]; \
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
#define REDUCT_SCRATCH_FREE(_reduct, _name) \
    do \
    { \
        assert((_reduct)->scratchSize > 0); \
        reduct_scratch_t* _s = &(_reduct)->scratch[--(_reduct)->scratchSize]; \
        free(_s->buffer); \
        _s->buffer = NULL; \
        _s->length = 0; \
        _name = NULL; \
    } while (0)

/**
 * @brief Hash a string.
 *
 * @param str The string to hash.
 * @param len The length of the string.
 * @return The hash of the string.
 */
static inline REDUCT_ALWAYS_INLINE uint32_t reduct_hash(const char* str, size_t len)
{
    uint32_t hash = REDUCT_FNV_OFFSET;
    for (size_t i = 0; i < len; i++)
    {
        hash ^= (unsigned char)str[i];
        hash *= REDUCT_FNV_PRIME;
    }
    return hash;
}

/** @} */

#endif
