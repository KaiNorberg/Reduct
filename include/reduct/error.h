#ifndef REDUCT_ERROR_H
#define REDUCT_ERROR_H 1

#include "reduct/defs.h"

struct reduct;
struct reduct_item;

#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>
#include <stdio.h>

/**
 * @file error.h
 * @brief Error handling and reporting.
 * @defgroup error Error
 *
 * @{
 */

#define REDUCT_ERROR_MAX_LEN 512             ///< Maximum length of an error string.
#define REDUCT_ERROR_BACKTRACE_MAX 16       ///< Maximum number of backtrace frames.

/**
 * @brief Error type enumeration.
 * @enum reduct_error_type_t
 */
typedef enum reduct_error_type
{
    REDUCT_ERROR_TYPE_NONE,
    REDUCT_ERROR_TYPE_SYNTAX,
    REDUCT_ERROR_TYPE_COMPILE,
    REDUCT_ERROR_TYPE_RUNTIME,
    REDUCT_ERROR_TYPE_INTERNAL,
} reduct_error_type_t;

/**
 * @brief Backtrace frame structure.
 * @struct reduct_error_frame_t
 *
 * Stores the source location for a single frame of the call stack at the time of a runtime error.
 */
typedef struct reduct_error_frame
{
    reduct_input_id_t inputId;  ///< The input ID of the source file.
    uint32_t position;   ///< The position in the input buffer.
} reduct_error_frame_t;

/**
 * @brief Error structure.
 * @struct reduct_error_t
 */
typedef struct
{
    const char* input;          ///< The input buffer.
    size_t inputLength;  ///< The total length of the input buffer.
    const char* path;           ///< THe path to the file that caused the error.
    size_t regionLength; ///< The length of the region that caused the error.
    size_t index;        ///< The index of the region in the input buffer that caused the error.
    jmp_buf jmp;
    reduct_error_type_t type; ///< The type of the error.
    char message[REDUCT_ERROR_MAX_LEN];
    struct reduct* reduct;                                       ///< The owning Reduct structure.
    reduct_error_frame_t frames[REDUCT_ERROR_BACKTRACE_MAX];     ///< Backtrace frames for the error.
    uint8_t frameCount;                                   ///< The number of backtrace frames.
} reduct_error_t;

/**
 * @brief Create a Reduct error structure.
 *
 * @return A new Reduct error structure initialized to zero.
 */
#define REDUCT_ERROR() ((reduct_error_t){0})

/**
 * @brief Format and print the error to a file.
 *
 * @param error Pointer to the error structure.
 * @param file The file to print to.
 */
REDUCT_API void reduct_error_print(reduct_error_t* error, FILE* file);

/**
 * @brief Get the row and column by traversing the input buffer.
 *
 * @param error Pointer to the error structure.
 * @param row Pointer to the row variable.
 * @param column Pointer to the column variable.
 */
REDUCT_API void reduct_error_get_row_column(reduct_error_t* error, size_t* row, size_t* column);

/**
 * @brief Set the error information in the error structure.
 *
 * @param error Pointer to the error structure.
 * @param path The path to the file where the error occurred.
 * @param input The input buffer where the error occurred.
 * @param inputLength The total length of the input buffer.
 * @param regionLength The length of the token/region that caused the error.
 * @param position The position in the input buffer where the error occurred.
 * @param type The type of the error.
 * @param message The error message format string.
 * @param ... The arguments for the format string.
 */
REDUCT_API void reduct_error_set(reduct_error_t* error, const char* path, const char* input, size_t inputLength,
    size_t regionLength, size_t position, reduct_error_type_t type, const char* message, ...);

/**
 * @brief Get the error parameters from a Reduct item.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param item Pointer to the item.
 * @param path Pointer to the path variable.
 * @param input Pointer to the input variable.
 * @param inputLength Pointer to the input length variable.
 * @param regionLength Pointer to the region length variable.
 * @param position Pointer to the position variable.
 */
REDUCT_API void reduct_error_get_item_params(struct reduct* reduct, struct reduct_item* item, const char** path, const char** input,
    size_t* inputLength, size_t* regionLength, size_t* position);

/**
 * @brief Throw a runtime error utilizing the evaluation state to determine the context.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param message The error message format string.
 * @param ... Additional arguments.
 */
REDUCT_API REDUCT_NORETURN void reduct_error_throw_runtime(struct reduct* reduct, const char* message, ...);

/**
 * @brief Catch an error using the jump buffer in the error structure.
 *
 * @param _error Pointer to the error structure.
 */
#define REDUCT_ERROR_CATCH(_error) (setjmp((_error)->jmp))

/**
 * @brief Throw an error using the jump buffer in the error structure.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _error Pointer to the error structure.
 * @param _item Pointer to the item that caused the error.
 * @param _type The suffix of the error type (e.g., INTERNAL, RUNTIME, etc.).
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_THROW(_reduct, _error, _item, _type, ...) \
    do \
    { \
        const char* __path; \
        const char* __input; \
        size_t __input_length; \
        size_t __region_length; \
        size_t __position; \
        reduct_error_get_item_params((_reduct), (_item), &__path, &__input, &__input_length, &__region_length, &__position); \
        reduct_error_set((_error), __path, __input, __input_length, __region_length, __position, \
            REDUCT_ERROR_TYPE_##_type, __VA_ARGS__); \
        longjmp((_error)->jmp, REDUCT_TRUE); \
    } while (0)

/**
 * @brief Throw a syntax error using the jump buffer in the error structure.
 *
 * @param _error Pointer to the error structure.
 * @param _input Pointer to the input structure being parsed.
 * @param _ptr Pointer to the current position in the input buffer.
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_SYNTAX(_error, _input, _ptr, ...) \
    do \
    { \
        reduct_error_set((_error), (_input)->path, (_input)->buffer, (_input)->end - (_input)->buffer, 1, \
            (size_t)((_ptr) - (_input)->buffer), REDUCT_ERROR_TYPE_SYNTAX, __VA_ARGS__); \
        longjmp((_error)->jmp, REDUCT_TRUE); \
    } while (0)

/**
 * @brief Throw a compile error using the jump buffer in the error structure.
 *
 * @param _compiler The compiler instance.
 * @param _item Pointer to the item that caused the error.
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_COMPILE(_compiler, _item, ...) \
    REDUCT_ERROR_THROW((_compiler->reduct), (_compiler)->reduct->error, \
        (((_item) != NULL && (_item)->inputId != REDUCT_INPUT_ID_NONE) \
                ? (_item) \
                : ((_compiler)->lastItem != NULL ? (_compiler)->lastItem : (_item))), \
        COMPILE, __VA_ARGS__)

/**
 * @brief Throw a runtime error using the jump buffer in the error structure.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_RUNTIME(_reduct, ...) reduct_error_throw_runtime((_reduct), __VA_ARGS__)

/**
 * @brief Throw a runtime error if the expression is false.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _expr The expression to check.
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_RUNTIME_ASSERT(_reduct, _expr, ...) \
    do \
    { \
        if (REDUCT_UNLIKELY(!(_expr))) \
        { \
            REDUCT_ERROR_RUNTIME(_reduct, __VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief Throw an internal error using the jump buffer in the error structure.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param ... The error message format string and any optional arguments.
 */
#define REDUCT_ERROR_INTERNAL(_reduct, ...) REDUCT_ERROR_THROW(reduct, (_reduct)->error, NULL, INTERNAL, __VA_ARGS__)

/** @} */

#endif
