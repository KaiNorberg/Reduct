#ifndef REDUCT_CHAR_H
#define REDUCT_CHAR_H 1

/**
 * @file char.h
 * @brief Character handling.
 * @defgroup char Characters
 *
 * Lookup tables are used to reduce branching and improve performance when parsing and processing characters.
 *
 * @{
 */

/**
 * @brief Character classification flags.
 * @enum reduct_char_flags_t
 *
 * Follows the grammar defined in the README.
 */
typedef enum reduct_char_flags
{
    REDUCT_CHAR_LETTER = (1 << 0),     ///< Is a letter.
    REDUCT_CHAR_DIGIT = (1 << 1),      ///< Is a decimal digit.
    REDUCT_CHAR_SYMBOL = (1 << 2),     ///< Is a symbol.
    REDUCT_CHAR_WHITESPACE = (1 << 3), ///< Is whitespace.
    REDUCT_CHAR_HEX_DIGIT = (1 << 4),  ///< Is a hexidecimal digit.
} reduct_char_flags_t;

/**
 * @brief Character information.
 * @struct reduct_char_info
 */
typedef struct reduct_char_info
{
    reduct_char_flags_t flags; ///< Character classification flags.
    char upper;                ///< Uppercase equivalent.
    char lower;                ///< Lowercase equivalent.
    char decodeEscape;         ///< The char to decode to when escaped.
    char encodeEscape;         ///< The char to use when encoding an escape.
    unsigned char integer;     ///< Integer value.
} reduct_char_info_t;

/**
 * @brief Global character lookup table.
 */
extern reduct_char_info_t reductCharTable[256];

/**
 * @brief Get the character flags for a given character.
 *
 * @param _c The character to get flags for.
 * @return The character flags.
 */
#define REDUCT_CHAR_FLAGS(_c) (reductCharTable[(unsigned char)(_c)].flags)

/**
 * @brief Get the lowercase equivalent of a character.
 *
 * @param _c The character to get the lowercase equivalent of.
 * @return The lowercase equivalent of the character.
 */
#define REDUCT_CHAR_TO_LOWER(_c) (reductCharTable[(unsigned char)(_c)].lower)

/**
 * @brief Get the uppercase equivalent of a character.
 *
 * @param _c The character to get the uppercase equivalent of.
 * @return The uppercase equivalent of the character.
 */
#define REDUCT_CHAR_TO_UPPER(_c) (reductCharTable[(unsigned char)(_c)].upper)

/**
 * @brief Check if a character is whitespace.
 *
 * @param _c The character to check.
 * @return true if the character is whitespace, false otherwise.
 */
#define REDUCT_CHAR_IS_WHITESPACE(_c) (REDUCT_CHAR_FLAGS(_c) & REDUCT_CHAR_WHITESPACE)

/**
 * @brief Check if a character is a letter.
 *
 * @param _c The character to check.
 * @return true if the character is a letter, false otherwise.
 */
#define REDUCT_CHAR_IS_LETTER(_c) (REDUCT_CHAR_FLAGS(_c) & REDUCT_CHAR_LETTER)

/**
 * @brief Check if a character is a decimal digit.
 *
 * @param _c The character to check.
 * @return true if the character is a decimal digit, false otherwise.
 */
#define REDUCT_CHAR_IS_DIGIT(_c) (REDUCT_CHAR_FLAGS(_c) & REDUCT_CHAR_DIGIT)

/**
 * @brief Check if a character is a symbol.
 *
 * @param _c The character to check.
 * @return true if the character is a symbol, false otherwise.
 */
#define REDUCT_CHAR_IS_SYMBOL(_c) (REDUCT_CHAR_FLAGS(_c) & REDUCT_CHAR_SYMBOL)

/**
 * @brief Check if a character is a hexidecimal digit.
 *
 * @param _c The character to check.
 * @return true if the character is a hexidecimal digit, false otherwise.
 */
#define REDUCT_CHAR_IS_HEX_DIGIT(_c) (REDUCT_CHAR_FLAGS(_c) & REDUCT_CHAR_HEX_DIGIT)

/** @} */

#endif
