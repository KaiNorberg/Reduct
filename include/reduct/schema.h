#ifndef REDUCT_SCHEMA_H
#define REDUCT_SCHEMA_H 1

#include <reduct/defs.h>
#include <reduct/sync.h>

#include <stdbool.h>

struct reduct;
typedef struct reduct_atom reduct_atom_t;

/**
 * @file schema.h
 * @brief Schema transformation.
 * @defgroup schema Schema
 *
 * Schemas provide a way to validate the structure of Reduct association lists and transform them
 * into native C structures.
 *
 * ## Applying a Schema
 *
 * The `reduct_schema_apply()` function expects an association list where each key corresponds to a field in the schema
 * and the value is the data for that field.
 *
 * Arrays are an exception to the above and can be specified in two ways:
 * - `("my-key" ("first" "second" "third"))`
 * - `("my-key" "first" "second" "third")`
 *
 * Enum fields are validated against a table of keywords registered with the schema. In strict mode an invalid keyword
 * throws an error naming both the keyword and the field.
 *
 * Application is lenient by default such that unknown keys, malformed entries, and invalid values are skipped
 * non-destructively. 
 *
 * Schemas are append-only. Once registered, a schema is never modified or freed before VM teardown, to allow
 * `reduct_schema_apply()` to run without holding any locks.
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
    REDUCT_SCHEMA_TYPE_HANDLE, ///< A `reduct_handle_t`.
    REDUCT_SCHEMA_TYPE_ARRAY,  ///< A fixed-size array of primitives.
    REDUCT_SCHEMA_TYPE_ATOM,   ///< A pointer to a `reduct_atom_t`.
    REDUCT_SCHEMA_TYPE_LIST,   ///< A pointer to a `reduct_list_t`.
    REDUCT_SCHEMA_TYPE_ENUM,   ///< An enumerator, validated against a keyword table and stored as an integer.
} reduct_schema_type_t;

/**
 * @brief A single keyword/value pair of an enum field.
 * @struct reduct_schema_enum_t
 */
typedef struct reduct_schema_enum
{
    const char* keyword; ///< The keyword as it appears in the association list, e.g. `"row"`.
    int32_t value;       ///< The value written to the C structure, e.g. an enum constant.
} reduct_schema_enum_t;

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
    union {
        struct
        {
            reduct_schema_type_t subtype;
            size_t elementSize;
        } array;
        struct
        {
            const reduct_schema_enum_t* entries; ///< The keyword table, must stay valid for the VM lifetime.
            size_t count;
        } enumeration;
    } as;
} reduct_schema_t;

typedef struct reduct_schema_internal reduct_schema_internal_t; ///< Internal schema representation.

typedef uint32_t reduct_schema_id_t; ///< Schema ID type.

typedef uint32_t reduct_schema_index_t; ///< Schema index type.

#define REDUCT_SCHEMA_INDEX_NONE ((reduct_schema_index_t) - 1) ///< Invalid schema index.

/**
 * @brief Global schema-related state structure.
 * @struct reduct_schema_global_t
 */
typedef struct
{
    struct reduct_schema_internal** schemas;
    size_t count;
    size_t capacity;
    reduct_rwmutex_t mutex;
} reduct_schema_global_t;

/**
 * @brief Apply flags for `reduct_schema_apply()`.
 * @enum reduct_schema_apply_flags_t
 */
typedef enum
{
    REDUCT_SCHEMA_APPLY_NORMAL = 0,             ///< No flags.
    REDUCT_SCHEMA_APPLY_STRICT_KEYS = 1 << 0,   ///< Throw on unknown keys.
    REDUCT_SCHEMA_APPLY_STRICT_VALUES = 1 << 1, ///< Throw on malformed or invalid values.
} reduct_schema_apply_flags_t;

/**
 * @brief Initialize a global schema state.
 *
 * @param global Pointer to the global schema state to initialize.
 */
REDUCT_API void reduct_schema_global_init(reduct_schema_global_t* global);

/**
 * @brief Deinitialize a global schema state.
 *
 * @param global Pointer to the global schema state to deinitialize.
 */
REDUCT_API void reduct_schema_global_deinit(reduct_schema_global_t* global);

/**
 * @brief Create a new schema from an array of fields.
 *
 * Schema keys and enum keywords are interned and retained for the lifetime of the VM, so lookups during application
 * are pointer comparisons.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param count Number of fields.
 * @param fields Array of field definitions.
 * @return The ID of the newly created schema.
 */
REDUCT_API reduct_schema_id_t reduct_schema_new(struct reduct* reduct, size_t count, const reduct_schema_t* fields);

/**
 * @brief Apply a schema to an association list and populate a C structure.
 *
 * Any fields not explicitly set by the given list are guaranteed to be left untouched.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The ID of the schema to apply.
 * @param listHandle The handle to the association list.
 * @param flags Apply flags.
 * @param out Pointer to the destination C structure.
 */
REDUCT_API void reduct_schema_apply(struct reduct* reduct, reduct_schema_id_t id, reduct_handle_t listHandle, reduct_schema_apply_flags_t flags, void* out);

/**
 * @brief Look up which field an atom corresponds to within a schema.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The schema ID.
 * @param keyAtom The atom to look up.
 * @return The index of the field, or `REDUCT_SCHEMA_INDEX_NONE` if the atom is not a key in the schema.
 */
REDUCT_API reduct_schema_index_t reduct_schema_find(struct reduct* reduct, reduct_schema_id_t id,
    reduct_atom_t* keyAtom);

/**
 * @brief Get the number of fields in a schema.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param id The schema ID.
 * @return The number of fields, or 0 if the ID is invalid.
 */
REDUCT_API size_t reduct_schema_get_count(struct reduct* reduct, reduct_schema_id_t id);

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
 * @brief Registers a schema, deriving the field count automatically.
 *
 * @param _reduct Pointer to the Reduct structure.
 * @param _schema A static array of `reduct_schema_t`.
 */
#define REDUCT_SCHEMA_NEW(_reduct, _schema) \
    reduct_schema_new((_reduct), sizeof(_schema) / sizeof((_schema)[0]), (_schema))

/**
 * @brief Helper macro to define a schema field.
 *
 * @param _struct The C structure type.
 * @param _key The key string in the association list.
 * @param _member The member name in the C structure.
 * @param _type The `reduct_schema_type_t` of the field, only the suffix is required, `REDUCT_SCHEMA_TYPE_` is added
 * automatically.
 */
#define REDUCT_SCHEMA_FIELD(_struct, _key, _member, _type) \
    (reduct_schema_t) \
    { \
        (_key), offsetof(_struct, _member), sizeof(((_struct*)0)->_member), REDUCT_SCHEMA_TYPE_##_type, \
        {{0}} \
    }

/**
 * @brief Helper macro to define an array schema field.
 *
 * @param _struct The C structure type.
 * @param _key The key string.
 * @param _member The array member name.
 * @param _type The `reduct_schema_type_t` of the field, only the suffix is required, `REDUCT_SCHEMA_TYPE_` is added
 * automatically.
 */
#define REDUCT_SCHEMA_FIELD_ARRAY(_struct, _key, _member, _subtype) \
    (reduct_schema_t) \
    { \
        (_key), offsetof(_struct, _member), sizeof(((_struct*)0)->_member), REDUCT_SCHEMA_TYPE_ARRAY, \
        { \
            .array = { REDUCT_SCHEMA_TYPE_##_subtype, sizeof(((_struct*)0)->_member[0]) } \
        } \
    }

/**
 * @brief Helper macro to define an enum schema field.
 *
 * The member must be an integer type capable of holding every value in the keyword table.
 *
 * @param _struct The C structure type.
 * @param _key The key string.
 * @param _member The integer member name.
 * @param _entries The static array of `reduct_schema_enum_t`, the array size is derived automatically.
 */
#define REDUCT_SCHEMA_FIELD_ENUM(_struct, _key, _member, _entries) \
    (reduct_schema_t) \
    { \
        (_key), offsetof(_struct, _member), sizeof(((_struct*)0)->_member), REDUCT_SCHEMA_TYPE_ENUM, \
        { \
            .enumeration = {(_entries), sizeof(_entries) / sizeof((_entries)[0]) } \
        } \
    }

/** @} */

#endif