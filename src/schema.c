#include "reduct/schema.h"
#include "reduct/atom.h"
#include "reduct/core.h"
#include "reduct/gc.h"
#include "reduct/standard.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

REDUCT_API reduct_schema_id_t reduct_schema_new(struct reduct* reduct, size_t count, ...)
{
    assert(reduct != NULL);
    assert(count > 0);

    if (reduct->schemaCount >= reduct->schemaCapacity)
    {
        size_t oldCapacity = reduct->schemaCapacity;
        reduct->schemaCapacity *= REDUCT_SCHEMA_GROWTH;
        if (reduct->schemaCapacity == 0)
        {
            reduct->schemaCapacity = REDUCT_SCHEMA_INITIAL;
        }
        reduct_schema_internal_t** newSchemas = (reduct_schema_internal_t**)realloc(reduct->schemas,
            reduct->schemaCapacity * sizeof(reduct_schema_internal_t*));
        if (newSchemas == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }

        memset(newSchemas + oldCapacity, 0, (reduct->schemaCapacity - oldCapacity) * sizeof(reduct_schema_internal_t*));
        reduct->schemas = newSchemas;
    }

    reduct_schema_id_t id = reduct->schemaCount++;
    reduct_schema_internal_t* schema =
        (reduct_schema_internal_t*)malloc(sizeof(reduct_schema_internal_t) + count * sizeof(reduct_schema_t));
    if (schema == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    schema->count = count;

    va_list args;
    va_start(args, count);
    for (size_t i = 0; i < count; i++)
    {
        schema->fields[i] = va_arg(args, reduct_schema_t);
    }
    va_end(args);

    reduct->schemas[id] = schema;

    for (size_t i = 0; i < count; i++)
    {
        reduct_atom_t* atom =
            reduct_atom_lookup(reduct, schema->fields[i].key, strlen(schema->fields[i].key), REDUCT_ATOM_LOOKUP_QUOTED);

        REDUCT_ERROR_RUNTIME_ASSERT(reduct, !(atom->flags & (REDUCT_ATOM_FLAG_INTEGER | REDUCT_ATOM_FLAG_FLOAT)),
            "schema key cannot be a number");

        if (!(atom->flags & REDUCT_ATOM_FLAG_SCHEMA))
        {
            atom->flags |= REDUCT_ATOM_FLAG_SCHEMA;

            atom->schema = NULL;
            atom->schemaCount = 0;
        }

        size_t newSchemaCount = atom->schemaCount;
        if (id >= newSchemaCount)
        {
            newSchemaCount = id + 1;
        }
        reduct_schema_index_t* newSchema =
            (reduct_schema_index_t*)realloc(atom->schema, newSchemaCount * sizeof(reduct_schema_index_t));
        if (newSchema == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        for (size_t j = atom->schemaCount; j < newSchemaCount; j++)
        {
            newSchema[j] = REDUCT_SCHEMA_INDEX_NONE;
        }

        atom->schema = newSchema;
        atom->schemaCount = (uint32_t)newSchemaCount;

        atom->schema[id] = i;
        reduct_gc_retain(reduct, REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
    }

    return id;
}

static void reduct_schema_apply_primitive(struct reduct* reduct, reduct_schema_type_t type, size_t size, void* target,
    reduct_handle_t* valH)
{
    switch (type)
    {
    case REDUCT_SCHEMA_TYPE_UINT:
    {
        switch (size)
        {
        case 1:
            *(uint8_t*)target = (uint8_t)reduct_handle_as_int(reduct, valH);
            break;
        case 2:
            *(uint16_t*)target = (uint16_t)reduct_handle_as_int(reduct, valH);
            break;
        case 4:
            *(uint32_t*)target = (uint32_t)reduct_handle_as_int(reduct, valH);
            break;
        case 8:
            *(uint64_t*)target = (uint64_t)reduct_handle_as_int(reduct, valH);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_INT:
    {
        switch (size)
        {
        case 1:
            *(int8_t*)target = (int8_t)reduct_handle_as_int(reduct, valH);
            break;
        case 2:
            *(int16_t*)target = (int16_t)reduct_handle_as_int(reduct, valH);
            break;
        case 4:
            *(int32_t*)target = (int32_t)reduct_handle_as_int(reduct, valH);
            break;
        case 8:
            *(int64_t*)target = (int64_t)reduct_handle_as_int(reduct, valH);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_FLOAT:
    {
        switch (size)
        {
        case 4:
            *(float*)target = (float)reduct_handle_as_float(reduct, valH);
            break;
        case 8:
            *(double*)target = (double)reduct_handle_as_float(reduct, valH);
            break;
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_BOOL:
    {
        *(bool*)target = REDUCT_HANDLE_IS_TRUTHY(valH);
    }
    break;
    case REDUCT_SCHEMA_TYPE_STRING:
    {
        if (!REDUCT_HANDLE_IS_ATOM(valH))
            return;
        const char* string;
        size_t stringLen;
        reduct_handle_atom_string(reduct, valH, &string, &stringLen);
        size_t copyLen = (stringLen < size - 1) ? stringLen : size - 1;
        memcpy(target, string, copyLen);
        ((char*)target)[copyLen] = '\0';
    }
    break;
    case REDUCT_SCHEMA_TYPE_HANDLE:
    {
        *(reduct_handle_t*)target = *valH;
    }
    break;
    default:
        break;
    }
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
            return REDUCT_HANDLE_FROM_INT(*(uint8_t*)val);
        case 2:
            return REDUCT_HANDLE_FROM_INT(*(uint16_t*)val);
        case 4:
            return REDUCT_HANDLE_FROM_INT(*(uint32_t*)val);
        case 8:
            return REDUCT_HANDLE_FROM_INT(*(uint64_t*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_INT:
    {
        switch (size)
        {
        case 1:
            return REDUCT_HANDLE_FROM_INT(*(int8_t*)val);
        case 2:
            return REDUCT_HANDLE_FROM_INT(*(int16_t*)val);
        case 4:
            return REDUCT_HANDLE_FROM_INT(*(int32_t*)val);
        case 8:
            return REDUCT_HANDLE_FROM_INT(*(int64_t*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_FLOAT:
    {
        switch (size)
        {
        case 4:
            return REDUCT_HANDLE_FROM_FLOAT((double)*(float*)val);
        case 8:
            return REDUCT_HANDLE_FROM_FLOAT(*(double*)val);
        }
    }
    break;
    case REDUCT_SCHEMA_TYPE_BOOL:
    {
        return *(bool*)val ? REDUCT_HANDLE_FROM_INT(1) : REDUCT_HANDLE_FROM_INT(0);
    }
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
    default:
        break;
    }
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API bool reduct_schema_apply(struct reduct* reduct, reduct_schema_id_t id, reduct_handle_t* listH, void* out)
{
    assert(reduct != NULL);
    assert(listH != NULL);
    assert(out != NULL);

    if (id >= reduct->schemaCount)
    {
        return false;
    }

    reduct_schema_internal_t* schema = reduct->schemas[id];
    if (schema == NULL)
    {
        return false;
    }

    if (!REDUCT_HANDLE_IS_LIST(listH))
    {
        return false;
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listH);

    reduct_handle_t entryH;
    REDUCT_LIST_FOR_EACH(&entryH, list)
    {
        reduct_handle_t keyH, valH;
        if (!reduct_list_get_entry(reduct, &entryH, &keyH, &valH))
        {
            continue;
        }

        if (!REDUCT_HANDLE_IS_ATOM(&keyH))
        {
            continue;
        }

        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(&keyH);
        if (!(atom->flags & REDUCT_ATOM_FLAG_SCHEMA) || id >= atom->schemaCount)
        {
            continue;
        }

        reduct_schema_index_t fieldIdx = atom->schema[id];
        if (fieldIdx == REDUCT_SCHEMA_INDEX_NONE)
        {
            continue;
        }

        const reduct_schema_t* field = &schema->fields[fieldIdx];
        void* target = (char*)out + field->offset;

        if (field->type == REDUCT_SCHEMA_TYPE_ARRAY)
        {
            if (!REDUCT_HANDLE_IS_LIST(&valH))
            {
                continue;
            }
            reduct_list_t* subList = REDUCT_HANDLE_TO_LIST(&valH);
            reduct_handle_t itemH;
            REDUCT_LIST_FOR_EACH(&itemH, subList)
            {
                if (_iter.index * field->elementSize >= field->size)
                {
                    break;
                }
                reduct_schema_apply_primitive(reduct, field->subtype, field->elementSize,
                    (char*)target + _iter.index * field->elementSize, &itemH);
            }
        }
        else
        {
            reduct_schema_apply_primitive(reduct, field->type, field->size, target, &valH);
        }
    }
    return true;
}

REDUCT_API reduct_handle_t reduct_schema_serialize(reduct_t* reduct, reduct_schema_id_t id, const void* in)
{
    assert(reduct != NULL);
    assert(in != NULL);

    if (id >= reduct->schemaCount)
    {
        REDUCT_ERROR_RUNTIME(reduct, "invalid schema ID");
    }

    reduct_schema_internal_t* schema = reduct->schemas[id];
    if (schema == NULL)
    {
        REDUCT_ERROR_RUNTIME(reduct, "invalid schema ID");
    }

    reduct_list_t* list = reduct_list_new(reduct);

    for (size_t i = 0; i < schema->count; i++)
    {
        const reduct_schema_t* field = &schema->fields[i];
        void* val = (char*)in + field->offset;

        reduct_list_t* pair = reduct_list_new(reduct);
        reduct_list_push(reduct, pair, REDUCT_HANDLE_CREATE_STRING(reduct, field->key));

        if (field->type == REDUCT_SCHEMA_TYPE_ARRAY)
        {
            reduct_list_t* subList = reduct_list_new(reduct);
            size_t count = field->size / field->elementSize;
            for (size_t j = 0; j < count; j++)
            {
                reduct_list_push(reduct, subList,
                    reduct_schema_serialize_primitive(reduct, field->subtype, field->elementSize,
                        (char*)val + j * field->elementSize));
            }
            reduct_list_push(reduct, pair, REDUCT_HANDLE_FROM_LIST(subList));
        }
        else
        {
            reduct_handle_t valH = reduct_schema_serialize_primitive(reduct, field->type, field->size, val);
            if (REDUCT_HANDLE_IS_NIL(&valH))
            {
                REDUCT_ERROR_RUNTIME(reduct, "invalid schema field type");
            }
            reduct_list_push(reduct, pair, valH);
        }
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_LIST(pair));
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}
