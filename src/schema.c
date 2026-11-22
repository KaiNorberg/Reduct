#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/error.h>
#include <reduct/gc.h>
#include <reduct/schema.h>
#include <reduct/standard.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct reduct_schema_lookup
{
    reduct_atom_t** atoms;
    uint32_t* indexes;
    uint32_t capacity;
} reduct_schema_lookup_t;

struct reduct_schema_internal
{
    size_t count;
    reduct_schema_lookup_t keys;
    reduct_schema_lookup_t* enums;
    reduct_schema_t fields[];
};

static uint32_t reduct_schema_hash_atom(reduct_atom_t* atom)
{
    uintptr_t value = (uintptr_t)atom;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return (uint32_t)value;
}

static reduct_schema_index_t reduct_schema_lookup_find(const reduct_schema_lookup_t* lookup, reduct_atom_t* atom)
{
    if (lookup->capacity == 0)
    {
        return REDUCT_SCHEMA_INDEX_NONE;
    }

    uint32_t slot = reduct_schema_hash_atom(atom) & (lookup->capacity - 1);
    while (lookup->atoms[slot] != NULL)
    {
        if (lookup->atoms[slot] == atom)
        {
            return (reduct_schema_index_t)lookup->indexes[slot];
        }
        slot = (slot + 1) & (lookup->capacity - 1);
    }
    return REDUCT_SCHEMA_INDEX_NONE;
}

static bool reduct_schema_lookup_build(reduct_schema_lookup_t* lookup, reduct_atom_t** atoms, uint32_t count)
{
    if (count == 0)
    {
        lookup->atoms = NULL;
        lookup->indexes = NULL;
        lookup->capacity = 0;
        return true;
    }

    uint32_t capacity = 4;
    while (capacity < count * 2)
    {
        capacity <<= 1;
    }

    reduct_atom_t** slots = (reduct_atom_t**)calloc(capacity, sizeof(reduct_atom_t*));
    uint32_t* indexes = (uint32_t*)malloc(capacity * sizeof(uint32_t));
    if (slots == NULL || indexes == NULL)
    {
        free(slots);
        free(indexes);
        return false;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t slot = reduct_schema_hash_atom(atoms[i]) & (capacity - 1);
        while (slots[slot] != NULL)
        {
            slot = (slot + 1) & (capacity - 1);
        }
        slots[slot] = atoms[i];
        indexes[slot] = i;
    }

    lookup->atoms = slots;
    lookup->indexes = indexes;
    lookup->capacity = capacity;
    return true;
}

REDUCT_API void reduct_schema_global_init(reduct_schema_global_t* global)
{
    assert(global != NULL);
    global->schemas = NULL;
    global->count = 0;
    global->capacity = 0;
    reduct_rwmutex_init(&global->mutex);
}

REDUCT_API void reduct_schema_global_deinit(reduct_schema_global_t* global)
{
    assert(global != NULL);
    if (global->schemas != NULL)
    {
        for (size_t i = 0; i < global->count; i++)
        {
            reduct_schema_internal_t* schema = global->schemas[i];
            if (schema == NULL)
            {
                continue;
            }
            for (size_t j = 0; j < schema->count; j++)
            {
                if (schema->enums[j].capacity != 0)
                {
                    free(schema->enums[j].atoms);
                    free(schema->enums[j].indexes);
                }
            }
            free(schema->keys.atoms);
            free(schema->keys.indexes);
            free(schema);
        }
        free(global->schemas);
    }
    reduct_rwmutex_destroy(&global->mutex);
}

REDUCT_API reduct_schema_id_t reduct_schema_new(struct reduct* reduct, size_t count, const reduct_schema_t* fields)
{
    assert(reduct != NULL);
    assert(count > 0);
    assert(fields != NULL);

    reduct_rwmutex_write_lock(&reduct->global->schema.mutex);
    if (reduct->global->schema.count >= reduct->global->schema.capacity)
    {
        size_t oldCapacity = reduct->global->schema.capacity;
        size_t newCapacity = oldCapacity * REDUCT_SCHEMA_GROWTH;
        if (newCapacity == 0)
        {
            newCapacity = REDUCT_SCHEMA_INITIAL;
        }
        reduct_schema_internal_t** newSchemas = (reduct_schema_internal_t**)realloc(reduct->global->schema.schemas,
            newCapacity * sizeof(reduct_schema_internal_t*));
        if (newSchemas == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->schema.mutex);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        memset(newSchemas + oldCapacity, 0, (newCapacity - oldCapacity) * sizeof(reduct_schema_internal_t*));
        reduct->global->schema.schemas = newSchemas;
        reduct->global->schema.capacity = newCapacity;
    }

    reduct_schema_id_t id = (reduct_schema_id_t)reduct->global->schema.count++;
    reduct_rwmutex_write_unlock(&reduct->global->schema.mutex);

    reduct_schema_internal_t* schema = (reduct_schema_internal_t*)malloc(
        sizeof(reduct_schema_internal_t) + count * sizeof(reduct_schema_t) + count * sizeof(reduct_schema_lookup_t));
    if (schema == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    schema->count = count;
    schema->keys.atoms = NULL;
    schema->keys.indexes = NULL;
    schema->keys.capacity = 0;
    schema->enums = (reduct_schema_lookup_t*)(schema->fields + count);
    memset(schema->enums, 0, count * sizeof(reduct_schema_lookup_t));
    for (size_t i = 0; i < count; i++)
    {
        schema->fields[i] = fields[i];
    }

    reduct_atom_t** keyAtoms = (reduct_atom_t**)malloc(count * sizeof(reduct_atom_t*));
    if (keyAtoms == NULL)
    {
        free(schema);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    for (size_t i = 0; i < count; i++)
    {
        reduct_atom_t* atom =
            reduct_atom_lookup(reduct, schema->fields[i].key, strlen(schema->fields[i].key), REDUCT_ATOM_LOOKUP_QUOTED);
        REDUCT_ERROR_ASSERT(reduct, !(atom->flags & REDUCT_ATOM_FLAG_NUMBER), "schema key cannot be a number");
        keyAtoms[i] = atom;
        reduct_atom_retain(reduct, atom);
    }

    if (!reduct_schema_lookup_build(&schema->keys, keyAtoms, (uint32_t)count))
    {
        free(keyAtoms);
        free(schema);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    for (size_t i = 0; i < count; i++)
    {
        const reduct_schema_t* field = &schema->fields[i];
        if (field->type != REDUCT_SCHEMA_TYPE_ENUM)
        {
            continue;
        }

        const size_t entryCount = field->as.enumeration.count;
        REDUCT_ERROR_ASSERT(reduct, entryCount == 0 || field->as.enumeration.entries != NULL,
            "schema enum field '%s' has no entry table", field->key);

        reduct_atom_t** valueAtoms = (reduct_atom_t**)malloc(entryCount * sizeof(reduct_atom_t*));
        if (entryCount > 0 && valueAtoms == NULL)
        {
            free(keyAtoms);
            free(schema);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        for (size_t j = 0; j < entryCount; j++)
        {
            const char* keyword = field->as.enumeration.entries[j].keyword;
            reduct_atom_t* atom = reduct_atom_lookup(reduct, keyword, strlen(keyword), REDUCT_ATOM_LOOKUP_QUOTED);
            REDUCT_ERROR_ASSERT(reduct, !(atom->flags & REDUCT_ATOM_FLAG_NUMBER),
                "schema enum keyword cannot be a number");
            valueAtoms[j] = atom;
            reduct_atom_retain(reduct, atom);
        }

        if (!reduct_schema_lookup_build(&schema->enums[i], valueAtoms, (uint32_t)entryCount))
        {
            free(valueAtoms);
            free(keyAtoms);
            free(schema);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }

        for (size_t j = 0; j < entryCount; j++)
        {
            if (reduct_schema_lookup_find(&schema->enums[i], valueAtoms[j]) != j)
            {
                REDUCT_ERROR_ASSERT(reduct, false, "duplicate enum keyword '%s' in schema field '%s'",
                    field->as.enumeration.entries[j].keyword, field->key);
            }
        }
        free(valueAtoms);
    }
    free(keyAtoms);

    reduct_rwmutex_write_lock(&reduct->global->schema.mutex);
    reduct->global->schema.schemas[id] = schema;
    reduct_rwmutex_write_unlock(&reduct->global->schema.mutex);
    return id;
}

static bool reduct_schema_apply_primitive(struct reduct* reduct, reduct_schema_type_t type, size_t size, void* target,
    reduct_handle_t valueHandle, const char* key, bool lenient)
{
    switch (type)
    {
    case REDUCT_SCHEMA_TYPE_UINT:
    {
        switch (size)
        {
        case 1:
            *(uint8_t*)target = (uint8_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 2:
            *(uint16_t*)target = (uint16_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 4:
            *(uint32_t*)target = (uint32_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 8:
            *(uint64_t*)target = (uint64_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_INT:
    {
        switch (size)
        {
        case 1:
            *(int8_t*)target = (int8_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 2:
            *(int16_t*)target = (int16_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 4:
            *(int32_t*)target = (int32_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        case 8:
            *(int64_t*)target = (int64_t)reduct_handle_as_int(reduct, valueHandle);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_FLOAT:
    {
        switch (size)
        {
        case 4:
            *(float*)target = (float)reduct_handle_as_number(reduct, valueHandle);
            break;
        case 8:
            *(double*)target = (double)reduct_handle_as_number(reduct, valueHandle);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_BOOL:
    {
        *(bool*)target = REDUCT_HANDLE_IS_TRUTHY(valueHandle);
    }
    break;
    case REDUCT_SCHEMA_TYPE_STRING:
    {
        const char* string;
        size_t stringLen;
        reduct_handle_atom_string(reduct, &valueHandle, &string, &stringLen);
        size_t copyLen = (stringLen < size - 1) ? stringLen : size - 1;
        memcpy(target, string, copyLen);
        ((char*)target)[copyLen] = '\0';
    }
    break;
    case REDUCT_SCHEMA_TYPE_HANDLE:
    {
        *(reduct_handle_t*)target = valueHandle;
    }
    break;
    case REDUCT_SCHEMA_TYPE_ATOM:
    {
        if (REDUCT_HANDLE_IS_NIL(valueHandle))
        {
            *(reduct_atom_t**)target = NULL;
            break;
        }
        if (!REDUCT_HANDLE_IS_ATOM_LIKE(valueHandle))
        {
            if (lenient)
            {
                return false;
            }
            REDUCT_ERROR_THROW(reduct, "expected an atom handle for '%s', got %s", key,
                REDUCT_HANDLE_GET_TYPE_STRING(valueHandle));
        }
        *(reduct_atom_t**)target = reduct_handle_as_atom(reduct, valueHandle);
    }
    break;
    case REDUCT_SCHEMA_TYPE_LIST:
    {
        if (REDUCT_HANDLE_IS_NIL(valueHandle))
        {
            *(reduct_list_t**)target = NULL;
            break;
        }
        if (!REDUCT_HANDLE_IS_LIST(valueHandle))
        {
            if (lenient)
            {
                return false;
            }
            REDUCT_ERROR_THROW(reduct, "expected a list handle for '%s', got %s", key,
                REDUCT_HANDLE_GET_TYPE_STRING(valueHandle));
        }
        *(reduct_list_t**)target = REDUCT_HANDLE_TO_LIST(valueHandle);
    }
    break;
    default:
        return false;
    }
    return true;
}

static reduct_handle_t reduct_schema_serialize_primitive(reduct_t* reduct, reduct_schema_type_t type, size_t size,
    const void* val)
{
    switch (type)
    {
    case REDUCT_SCHEMA_TYPE_UINT:
    {
        switch (size)
        {
        case 1:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(uint8_t*)val);
        case 2:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(uint16_t*)val);
        case 4:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(uint32_t*)val);
        case 8:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(uint64_t*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_INT:
    {
        switch (size)
        {
        case 1:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(int8_t*)val);
        case 2:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(int16_t*)val);
        case 4:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(int32_t*)val);
        case 8:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(int64_t*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_FLOAT:
    {
        switch (size)
        {
        case 4:
            return REDUCT_HANDLE_FROM_NUMBER((double)*(float*)val);
        case 8:
            return REDUCT_HANDLE_FROM_NUMBER(*(double*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_BOOL:
        return REDUCT_HANDLE_FROM_BOOL(reduct, *(bool*)val);
        break;
    case REDUCT_SCHEMA_TYPE_STRING:
    {
        return REDUCT_HANDLE_CREATE_STRING(reduct, (char*)val);
    }
    break;
    case REDUCT_SCHEMA_TYPE_HANDLE:
    {
        return *(reduct_handle_t*)val;
    }
    break;
    case REDUCT_SCHEMA_TYPE_ATOM:
    {
        reduct_atom_t* atom = *(reduct_atom_t**)val;
        return atom != NULL ? REDUCT_HANDLE_FROM_ATOM(atom) : REDUCT_HANDLE_NIL(reduct);
    }
    break;
    case REDUCT_SCHEMA_TYPE_LIST:
    {
        reduct_list_t* list = *(reduct_list_t**)val;
        return list != NULL ? REDUCT_HANDLE_FROM_LIST(list) : REDUCT_HANDLE_NIL(reduct);
    }
    break;
    default:
        break;
    }
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API void reduct_schema_apply(struct reduct* reduct, reduct_schema_id_t id, reduct_handle_t listHandle, reduct_schema_apply_flags_t flags, void* out)

{
    assert(reduct != NULL);
    assert(out != NULL);

    reduct_rwmutex_read_lock(&reduct->global->schema.mutex);
    if (id >= reduct->global->schema.count || reduct->global->schema.schemas[id] == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        REDUCT_ERROR_THROW(reduct, "invalid schema ID");
    }
    reduct_schema_internal_t* schema = reduct->global->schema.schemas[id];
    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);

    if (!REDUCT_HANDLE_IS_LIST(listHandle))
    {
        REDUCT_ERROR_THROW(reduct, "invalid list handle");
    }

    const bool strictKeys = (flags & REDUCT_SCHEMA_APPLY_STRICT_KEYS) != 0;
    const bool strictValues = (flags & REDUCT_SCHEMA_APPLY_STRICT_VALUES) != 0;
    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listHandle);

    for (uint32_t i = 0; i < list->length; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(list->handles[i]))
        {
            continue;
        }

        reduct_list_t* entry = REDUCT_HANDLE_TO_LIST(list->handles[i]);
        if (entry->length < 2)
        {
            continue;
        }
        reduct_handle_t keyHandle = entry->handles[0];

        if (!REDUCT_HANDLE_IS_ATOM(keyHandle))
        {
            continue;
        }

        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(keyHandle);
        reduct_schema_index_t fieldIdx = reduct_schema_lookup_find(&schema->keys, atom);
        if (fieldIdx == REDUCT_SCHEMA_INDEX_NONE)
        {
            if (strictKeys)
            {
                REDUCT_ERROR_THROW(reduct, "unknown schema key '%s'", atom->string);
            }
            continue;
        }

        const reduct_schema_t* field = &schema->fields[fieldIdx];
        void* target = (char*)out + field->offset;

        if (field->type == REDUCT_SCHEMA_TYPE_ARRAY)
        {
            if (REDUCT_HANDLE_IS_LIST(entry->handles[1]))
            {
                reduct_list_t* subList = REDUCT_HANDLE_TO_LIST(entry->handles[1]);
                for (uint32_t j = 0; j < subList->length; j++)
                {
                    if (j * field->as.array.elementSize >= field->size)
                    {
                        break;
                    }
                    reduct_schema_apply_primitive(reduct, field->as.array.subtype, field->as.array.elementSize,
                        (char*)target + j * field->as.array.elementSize, subList->handles[j], field->key,
                        !strictValues);
                }
            }
            else
            {
                for (uint32_t j = 1; j < entry->length; j++)
                {
                    if ((j - 1) * field->as.array.elementSize >= field->size)
                    {
                        break;
                    }
                    reduct_schema_apply_primitive(reduct, field->as.array.subtype, field->as.array.elementSize,
                        (char*)target + (j - 1) * field->as.array.elementSize, entry->handles[j], field->key,
                        !strictValues);
                }
            }
            continue;
        }

        if (field->type == REDUCT_SCHEMA_TYPE_ENUM)
        {
            reduct_handle_t valueHandle = entry->handles[1];
            if (!REDUCT_HANDLE_IS_ATOM(valueHandle))
            {
                if (strictValues)
                {
                    REDUCT_ERROR_THROW(reduct, "expected an atom handle for '%s', got %s", field->key,
                        REDUCT_HANDLE_GET_TYPE_STRING(valueHandle));
                }
                continue;
            }

            reduct_schema_index_t entryIdx =
                reduct_schema_lookup_find(&schema->enums[fieldIdx], REDUCT_HANDLE_TO_ATOM(valueHandle));
            if (entryIdx == REDUCT_SCHEMA_INDEX_NONE)
            {
                if (strictValues)
                {
                    REDUCT_ERROR_THROW(reduct, "invalid value '%s' for '%s'",
                        REDUCT_HANDLE_TO_ATOM(valueHandle)->string, field->key);
                }
                continue;
            }

            const int32_t enumValue = field->as.enumeration.entries[entryIdx].value;
            switch (field->size)
            {
            case 1:
                *(int8_t*)target = (int8_t)enumValue;
                break;
            case 2:
                *(int16_t*)target = (int16_t)enumValue;
                break;
            case 4:
                *(int32_t*)target = enumValue;
                break;
            case 8:
                *(int64_t*)target = (int64_t)enumValue;
                break;
            default:
                break;
            }
            continue;
        }

        reduct_schema_apply_primitive(reduct, field->type, field->size, target, entry->handles[1], field->key,
            !strictValues);
    }
}

REDUCT_API reduct_schema_index_t reduct_schema_find(struct reduct* reduct, reduct_schema_id_t id,
    reduct_atom_t* keyAtom)
{
    assert(reduct != NULL);
    assert(keyAtom != NULL);

    reduct_rwmutex_read_lock(&reduct->global->schema.mutex);
    if (id >= reduct->global->schema.count || reduct->global->schema.schemas[id] == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        return REDUCT_SCHEMA_INDEX_NONE;
    }
    reduct_schema_internal_t* schema = reduct->global->schema.schemas[id];
    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);

    return reduct_schema_lookup_find(&schema->keys, keyAtom);
}

REDUCT_API size_t reduct_schema_get_count(struct reduct* reduct, reduct_schema_id_t id)
{
    assert(reduct != NULL);
    reduct_rwmutex_read_lock(&reduct->global->schema.mutex);
    if (id >= reduct->global->schema.count || reduct->global->schema.schemas[id] == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        return 0;
    }
    size_t count = reduct->global->schema.schemas[id]->count;
    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
    return count;
}

REDUCT_API reduct_handle_t reduct_schema_serialize(reduct_t* reduct, reduct_schema_id_t id, const void* in)
{
    assert(reduct != NULL);
    assert(in != NULL);

    reduct_rwmutex_read_lock(&reduct->global->schema.mutex);
    if (id >= reduct->global->schema.count || reduct->global->schema.schemas[id] == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        REDUCT_ERROR_THROW(reduct, "invalid schema ID");
    }
    reduct_schema_internal_t* schema = reduct->global->schema.schemas[id];
    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);

    reduct_list_t* list = reduct_list_new(reduct, schema->count);

    for (size_t i = 0; i < schema->count; i++)
    {
        const reduct_schema_t* field = &schema->fields[i];
        void* val = (char*)in + field->offset;

        reduct_list_t* pair = reduct_list_new(reduct, 2);
        pair->handles[0] = REDUCT_HANDLE_CREATE_STRING(reduct, field->key);

        if (field->type == REDUCT_SCHEMA_TYPE_ARRAY)
        {
            size_t count = field->size / field->as.array.elementSize;
            reduct_list_t* subList = reduct_list_new(reduct, count);
            for (size_t j = 0; j < count; j++)
            {
                subList->handles[j] = reduct_schema_serialize_primitive(reduct, field->as.array.subtype,
                    field->as.array.elementSize, (char*)val + j * field->as.array.elementSize);
            }
            pair->handles[1] = REDUCT_HANDLE_FROM_LIST(subList);
        }
        else if (field->type == REDUCT_SCHEMA_TYPE_ENUM)
        {
            const int32_t enumValue = *(int32_t*)val;
            const char* keyword = NULL;
            for (size_t j = 0; j < field->as.enumeration.count; j++)
            {
                if (field->as.enumeration.entries[j].value == enumValue)
                {
                    keyword = field->as.enumeration.entries[j].keyword;
                    break;
                }
            }
            pair->handles[1] = keyword != NULL ? REDUCT_HANDLE_CREATE_STRING(reduct, (char*)keyword)
                                               : REDUCT_HANDLE_FROM_NUMBER((double)enumValue);
        }
        else
        {
            reduct_handle_t valueHandle = reduct_schema_serialize_primitive(reduct, field->type, field->size, val);
            pair->handles[1] = valueHandle;
        }

        list->handles[i] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}