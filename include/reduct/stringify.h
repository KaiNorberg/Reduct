#ifndef REDUCT_STRINGIFY_H
#define REDUCT_STRINGIFY_H 1

#include "reduct/core.h"
#include "reduct/handle.h"

/**
 * @file stringify.h
 * @brief Stringification.
 * @defgroup stringify Stringification
 *
 * @{
 */

/**
 * @brief Converts a Reduct handle to its string representation.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param handle The handle to stringify.
 * @param buffer The destination buffer.
 * @param size The size of the destination buffer.
 * @return The number of characters that would have been written if the buffer was large enough, excluding the null
 * terminator.
 */
REDUCT_API size_t reduct_stringify(reduct_t* reduct, reduct_handle_t* handle, char* buffer, size_t size);

/** @} */

#endif
