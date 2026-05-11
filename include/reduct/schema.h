#ifndef REDUCT_SCHEMA_H
#define REDUCT_SCHEMA_H 1

#include "reduct/defs.h"

#include <stdbool.h>

struct reduct;

/**
 * @file schema.h
 * @brief Schema transformation.
 * @defgroup schema Schema
 *
 * Schemas provide a way to validate the structure of Reduct association lists and transform them
 * into native C structures.
 *
 * @{
 */

/**
 * @brief Schema type flags.
 * @enum reduct_schema_type_t
 */
typedef enum reduct_schema_type
{
    REDUCT_SCHEMA_TYPE_UINT,   ///< Unsigned integer.
    REDUCT_SCHEMA_TYPE_INT,    ///< Signed integer.
    REDUCT_SCHEMA_TYPE_FLOAT,  ///< Float or double.
    REDUCT_SCHEMA_TYPE_BOOL,   ///< A `bool`.
    REDUCT_SCHEMA_TYPE_STRING, ///< An array of characters.
    REDUCT_SCHEMA_TYPE_HANDLE  ///< A `reduct_handle_t`.
} reduct_schema_type_t;

/**
 * @brief Schema field structure.
 * @struct reduct_schema_t
 */
typedef struct reduct_schema
{
    const char* key;
    size_t offset;
    size_t size;
    reduct_schema_type_t type;
} reduct_schema_t;

/**
 * @brief Internal schema structure.
 * @struct reduct_schema_internal_t
 */
typedef struct reduct_schema_internal
{
    size_t count;
    reduct_schema_t fields[];
} reduct_schema_internal_t;

typedef uint32_t reduct_schema_id_t; ///< Schema ID type.

typedef uint32_t reduct_schema_index_t; ///< Schema index type.

#define REDUCT_SCHEMA_INDEX_NONE ((reduct_schema_index_t) - 1) ///< Invalid schema index.

/**
 * @brief Create a new schema.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count Number of fields.
 * @param ... Variadic arguments for specifying the fields.
 * @return The ID of the newly created schema.
 */
REDUCT_API reduct_schema_id_t reduct_schema_new(struct reduct* reduct, size_t count, ...);

/**
 * @brief Apply a schema to an association list and populate a C structure.
 *
 * Any fields not explicitly set by the given list are guaranteed to be left untouched.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the schema to apply.
 * @param listH The handle to the association list.
 * @param out Pointer to the destination C structure.
 * @return `true` if the schema was applied successfully, `false` otherwise.
 */
REDUCT_API bool reduct_schema_apply(struct reduct* reduct, reduct_schema_id_t id, reduct_handle_t* listH,
    void* out);

/**
 * @brief Transform a C structure into an association list using a schema.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the schema to use.
 * @param in Pointer to the source C structure.
 * @return A handle to the newly created association list.
 */
REDUCT_API reduct_handle_t reduct_schema_serialize(struct reduct* reduct, reduct_schema_id_t id, const void* in);

/**
 * @brief Helper macro to define a schema field.
 *
 * @param _key The key string in the association list.
 * @param _struct The C structure type.
 * @param _member The member name in the C structure.
 * @param _type The `reduct_schema_type_t` of the field, only the suffix is required, `REDUCT_SCHEMA_TYPE_` is added
 * automatically.
 */
#define REDUCT_SCHEMA_FIELD(_key, _struct, _member, _type) \
    (reduct_schema_t) \
    { \
        (_key), offsetof(_struct, _member), sizeof(((_struct*)0)->_member), REDUCT_SCHEMA_TYPE_##_type \
    }

/** @} */

#endif
