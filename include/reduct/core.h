#ifndef REDUCT_CORE_H
#define REDUCT_CORE_H 1

#include "reduct/defs.h"

struct reduct_item;

#include "reduct/atom.h"
#include "reduct/error.h"
#include "reduct/item.h"
#include "reduct/list.h"
#include "reduct/eval.h"
#include "reduct/native.h"

/**
 * @file core.h
 * @brief Core definitions and structures.
 * @defgroup core
 *
 * @{
 */

#define REDUCT_IMPORT_PATHS_INITIAL 4 ///< Initial size of the import path array.
#define REDUCT_IMPORT_PATHS_GROWTH 2 ///< Growth factor of the import path array.

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
 * @brief Constant structure.
 * @struct reduct_constant_t
 */
typedef struct reduct_constant
{
    struct reduct_atom* name;
    struct reduct_item* item;
} reduct_constant_t;

/**
 * @brief Scratch buffer structure.
 * @struct reduct_scratch_t
 */
typedef struct reduct_scratch
{
    char* buffer;
    reduct_size_t length;
} reduct_scratch_t;

#define REDUCT_SCRATCH_INITIAL 128 ///< Initial scratch buffer size.
#define REDUCT_SCRATCH_MAX 16 ///< The maximum number of scratch buffers.

#define REDUCT_LIBS_INITIAL 4 ///< Initial size of the library array.
#define REDUCT_LIBS_GROWTH 2 ///< Growth factor of the library array.

/**
 * @brief State structure.
 * @struct reduct_t
 */
typedef struct reduct
{
    reduct_eval_frame_t* frames;
    reduct_size_t frameCount;
    reduct_size_t frameCapacity;
    reduct_handle_t* regs;
    reduct_size_t regCount;
    reduct_size_t regCapacity;
    reduct_item_t** retained;
    reduct_size_t retainedCount;
    reduct_size_t retainedCapacity;
    reduct_size_t prevBlockCount;
    reduct_size_t blockCount;
    reduct_item_block_t* block;
    reduct_size_t freeCount;
    reduct_item_t* freeList;
    reduct_atom_stack_t* atomStack;
    reduct_size_t atomMapSize;
    reduct_size_t atomMapTombstones;
    reduct_size_t atomMapCapacity;
    reduct_size_t atomMapMask;
    struct reduct_atom** atomMap;
    reduct_size_t nativeMapSize;
    reduct_size_t nativeMapCapacity;
    reduct_size_t nativeMapMask;
    reduct_native_entry_t* nativeMap;
    reduct_size_t scratchSize;
    reduct_size_t scratchCapacity;
    reduct_scratch_t scratch[REDUCT_SCRATCH_MAX];
    reduct_input_t* input;
    reduct_input_t firstInput;
    reduct_item_t* trueItem;
    reduct_item_t* falseItem;
    reduct_item_t* nilItem;
    reduct_item_t* piItem;
    reduct_item_t* eItem;
    reduct_uint32_t constantCount;
    reduct_constant_t constants[REDUCT_CONSTANTS_MAX];
    reduct_jmp_buf_t jmp;
    reduct_error_t* error;
    reduct_input_id_t newInputId;
    reduct_lib_t* libs;
    reduct_size_t libCount;
    reduct_size_t libCapacity;
    char** importPaths;
    reduct_size_t importPathCount;
    reduct_size_t importPathCapacity;
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
 * @brief Register a constant in a Reduct structure.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param name The name of the constant.
 * @param item The item associated with the constant.
 */
REDUCT_API void reduct_constant_register(reduct_t* reduct, const char* name, struct reduct_item* item);

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
REDUCT_API reduct_input_t* reduct_input_new(reduct_t* reduct, const char* buffer, reduct_size_t length,
    const char* path, reduct_input_flags_t flags);

/**
 * @brief Lookup an input structure by its ID.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the input structure.
 * @return A pointer to the input structure, or `REDUCT_NULL` if not found.
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
    _type* _name = REDUCT_NULL; \
    do \
    { \
        reduct_size_t _needed = (_length) * sizeof(_type); \
        if ((_reduct)->scratchSize >= REDUCT_SCRATCH_MAX) \
        { \
            REDUCT_ERROR_INTERNAL(_reduct, "scratch buffer overflow"); \
        } \
        reduct_scratch_t* _s = &(_reduct)->scratch[(_reduct)->scratchSize++]; \
        _s->buffer = REDUCT_MALLOC(_needed); \
        if (_s->buffer == REDUCT_NULL) \
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
        reduct_size_t _needed = (_length) * sizeof(_type); \
        reduct_scratch_t* _s = &(_reduct)->scratch[(_reduct)->scratchSize - 1]; \
        _s->buffer = REDUCT_REALLOC(_s->buffer, _needed); \
        if (_s->buffer == REDUCT_NULL) \
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
        REDUCT_ASSERT((_reduct)->scratchSize > 0); \
        reduct_scratch_t* _s = &(_reduct)->scratch[--(_reduct)->scratchSize]; \
        REDUCT_FREE(_s->buffer); \
        _s->buffer = REDUCT_NULL; \
        _s->length = 0; \
        _name = REDUCT_NULL; \
    } while (0)

/**
 * @brief Hash a string.
 *
 * @param str The string to hash.
 * @param len The length of the string.
 * @return The hash of the string.
 */
static inline REDUCT_ALWAYS_INLINE reduct_uint32_t reduct_hash(const char* str, reduct_size_t len)
{
    reduct_uint32_t hash = REDUCT_FNV_OFFSET;
    for (reduct_size_t i = 0; i < len; i++)
    {
        hash ^= (unsigned char)str[i];
        hash *= REDUCT_FNV_PRIME;
    }
    return hash;
}

/** @} */

#endif
