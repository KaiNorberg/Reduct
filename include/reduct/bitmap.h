#ifndef REDUCT_BITMAP_H
#define REDUCT_BITMAP_H 1

#include <reduct/defs.h>

/**
 * @file bitmap.h
 * @brief Bitmap utilities.
 * @defgroup bitmap Bitmap
 *
 * @{
 */

typedef uint64_t reduct_bitmap_t; ///< A single word in a bitmap.

#define REDUCT_BITMAP_WIDTH 64 ///< The number of bits in a bitmap word.

#define REDUCT_BITMAP_INDEX_NONE (~(uint64_t)0) ///< Invalid bitmap index.

/**
 * @brief Calculate the number of 64-bit words needed for a bitmap of a given size.
 *
 * @param _size The number of bits.
 */
#define REDUCT_BITMAP_SIZE(_size) (((_size) + (REDUCT_BITMAP_WIDTH - 1)) / REDUCT_BITMAP_WIDTH)

/**
 * @brief Set a bit in a bitmap.
 *
 * @param _bitmap The bitmap array.
 * @param _bit The bit index to set.
 */
#define REDUCT_BITMAP_SET(_bitmap, _bit) \
    ((_bitmap)[(_bit) / REDUCT_BITMAP_WIDTH] |= (1ULL << ((_bit) % REDUCT_BITMAP_WIDTH)))

/**
 * @brief Clear a bit in a bitmap.
 *
 * @param _bitmap The bitmap array.
 * @param _bit The bit index to clear.
 */
#define REDUCT_BITMAP_CLEAR(_bitmap, _bit) \
    ((_bitmap)[(_bit) / REDUCT_BITMAP_WIDTH] &= ~(1ULL << ((_bit) % REDUCT_BITMAP_WIDTH)))

/**
 * @brief Check if a bit is set in a bitmap.
 *
 * @param _bitmap The bitmap array.
 * @param _bit The bit index to check.
 * @return Non-zero if the bit is set, zero otherwise.
 */
#define REDUCT_BITMAP_TEST(_bitmap, _bit) \
    (((_bitmap)[(_bit) / REDUCT_BITMAP_WIDTH] & (1ULL << ((_bit) % REDUCT_BITMAP_WIDTH))) != 0)

/**
 * @brief Find the first clear bit in a bitmap.
 *
 * @param bitmap The bitmap array.
 * @param size The number of words in the bitmap array.
 * @return The index of the first clear bit, or `REDUCT_BITMAP_INDEX_NONE` if all bits are set.
 */
static inline size_t reduct_bitmap_find_first_clear(const reduct_bitmap_t* bitmap, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (bitmap[i] != ~(uint64_t)0)
        {
            for (uint32_t b = 0; b < REDUCT_BITMAP_WIDTH; b++)
            {
                if (!(bitmap[i] & (1ULL << b)))
                {
                    return i * REDUCT_BITMAP_WIDTH + b;
                }
            }
        }
    }
    return REDUCT_BITMAP_INDEX_NONE;
}

/**
 * @brief Find the next set bit in a bitmap starting from a given index.
 *
 * @param bitmap The bitmap array.
 * @param size The number of words in the bitmap array.
 * @param start The bit index to start searching from.
 * @return The index of the next set bit, or `REDUCT_BITMAP_INDEX_NONE` if no more bits are set.
 */
static inline size_t reduct_bitmap_next_set(const reduct_bitmap_t* bitmap, size_t size, size_t start)
{
    size_t i = start / REDUCT_BITMAP_WIDTH;
    if (i >= size)
    {
        return REDUCT_BITMAP_INDEX_NONE;
    }

    uint32_t b = (uint32_t)(start % REDUCT_BITMAP_WIDTH);
    reduct_bitmap_t word = bitmap[i] & (~0ULL << b);

    if (word != 0)
    {
        for (; b < REDUCT_BITMAP_WIDTH; b++)
        {
            if (word & (1ULL << b))
            {
                return i * REDUCT_BITMAP_WIDTH + b;
            }
        }
    }

    for (i++; i < size; i++)
    {
        if (bitmap[i] != 0)
        {
            for (b = 0; b < REDUCT_BITMAP_WIDTH; b++)
            {
                if (bitmap[i] & (1ULL << b))
                {
                    return i * REDUCT_BITMAP_WIDTH + b;
                }
            }
        }
    }

    return REDUCT_BITMAP_INDEX_NONE;
}

/**
 * @brief Find the next clear bit in a bitmap starting from a given index.
 *
 * @param bitmap The bitmap array.
 * @param size The number of words in the bitmap array.
 * @param start The bit index to start searching from.
 * @return The index of the next clear bit, or `REDUCT_BITMAP_INDEX_NONE` if no more bits are clear.
 */
static inline size_t reduct_bitmap_next_clear(const reduct_bitmap_t* bitmap, size_t size, size_t start)
{
    size_t i = start / REDUCT_BITMAP_WIDTH;
    if (i >= size)
    {
        return REDUCT_BITMAP_INDEX_NONE;
    }

    uint32_t b = (uint32_t)(start % REDUCT_BITMAP_WIDTH);
    reduct_bitmap_t word = (~bitmap[i]) & (~0ULL << b);

    if (word != 0)
    {
        for (; b < REDUCT_BITMAP_WIDTH; b++)
        {
            if (word & (1ULL << b))
            {
                return i * REDUCT_BITMAP_WIDTH + b;
            }
        }
    }

    for (i++; i < size; i++)
    {
        if (bitmap[i] != ~(uint64_t)0)
        {
            for (b = 0; b < REDUCT_BITMAP_WIDTH; b++)
            {
                if (!(bitmap[i] & (1ULL << b)))
                {
                    return i * REDUCT_BITMAP_WIDTH + b;
                }
            }
        }
    }

    return REDUCT_BITMAP_INDEX_NONE;
}

/** @} */

#endif
