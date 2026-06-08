#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/gc.h>
#include <reduct/schema.h>
#include <reduct/standard.h>

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
            if (global->schemas[i] != NULL)
            {
                free(global->schemas[i]);
            }
        }
        free(global->schemas);
    }
    reduct_rwmutex_destroy(&global->mutex);
}

REDUCT_API reduct_schema_id_t reduct_schema_new(struct reduct* reduct, size_t count, ...)
{
    REDUCT_SCRATCH_GET(reduct, fields, reduct_schema_t, count);

    va_list args;
    va_start(args, count);
    for (size_t i = 0; i < count; i++)
    {
        fields[i] = va_arg(args, reduct_schema_t);
    }
    va_end(args);

    reduct_schema_id_t id = reduct_schema_new_fields(reduct, count, fields);
    REDUCT_SCRATCH_PUT(reduct, fields);
    return id;
}

REDUCT_API reduct_schema_id_t reduct_schema_new_fields(struct reduct* reduct, size_t count,
    const reduct_schema_t* fields)
{
    assert(reduct != NULL);
    assert(count > 0);

    reduct_rwmutex_write_lock(&reduct->global->schema.mutex);

    if (reduct->global->schema.count >= reduct->global->schema.capacity)
    {
        size_t oldCapacity = reduct->global->schema.capacity;
        reduct->global->schema.capacity *= REDUCT_SCHEMA_GROWTH;
        if (reduct->global->schema.capacity == 0)
        {
            reduct->global->schema.capacity = REDUCT_SCHEMA_INITIAL;
        }
        reduct_schema_internal_t** newSchemas = (reduct_schema_internal_t**)realloc(reduct->global->schema.schemas,
            reduct->global->schema.capacity * sizeof(reduct_schema_internal_t*));
        if (newSchemas == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->schema.mutex);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }

        memset(newSchemas + oldCapacity, 0,
            (reduct->global->schema.capacity - oldCapacity) * sizeof(reduct_schema_internal_t*));
        reduct->global->schema.schemas = newSchemas;
    }

    reduct_schema_id_t id = (reduct_schema_id_t)reduct->global->schema.count++;
    reduct_schema_internal_t* schema =
        (reduct_schema_internal_t*)malloc(sizeof(reduct_schema_internal_t) + count * sizeof(reduct_schema_t));
    if (schema == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    schema->count = count;
    for (size_t i = 0; i < count; i++)
    {
        schema->fields[i] = fields[i];
    }

    reduct->global->schema.schemas[id] = schema;

    for (size_t i = 0; i < count; i++)
    {
        reduct_atom_t* atom =
            reduct_atom_lookup(reduct, schema->fields[i].key, strlen(schema->fields[i].key), REDUCT_ATOM_LOOKUP_QUOTED);

        REDUCT_ERROR_ASSERT(reduct, !(atom->flags & REDUCT_ATOM_FLAG_NUMBER), "schema key cannot be a number");

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
        reduct_item_retain(REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
    }

    reduct_rwmutex_write_unlock(&reduct->global->schema.mutex);
    return id;
}

static void reduct_schema_apply_primitive(struct reduct* reduct, reduct_schema_type_t type, size_t size, void* target,
    reduct_handle_t valueHandle)
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
        if (!REDUCT_HANDLE_IS_ATOM(valueHandle))
            return;
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
    default:
        break;
    }
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API bool reduct_schema_apply(struct reduct* reduct, reduct_schema_id_t id, reduct_handle_t listHandle, void* out)
{
    assert(reduct != NULL);
    assert(out != NULL);

    reduct_rwmutex_read_lock(&reduct->global->schema.mutex);

    if (id >= reduct->global->schema.count)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        return false;
    }

    reduct_schema_internal_t* schema = reduct->global->schema.schemas[id];
    if (schema == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        return false;
    }

    if (!REDUCT_HANDLE_IS_LIST(listHandle))
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        return false;
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listHandle);

    for (uint32_t i = 0; i < list->length; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(list->handles[i]))
        {
            continue;
        }

        reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(list->handles[i]);
        if (pair->length < 2)
        {
            continue;
        }
        reduct_handle_t keyHandle = pair->handles[0];
        reduct_handle_t valueHandle = pair->handles[1];

        if (!REDUCT_HANDLE_IS_ATOM(keyHandle))
        {
            continue;
        }

        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(keyHandle);
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
            if (!REDUCT_HANDLE_IS_LIST(valueHandle))
            {
                continue;
            }

            reduct_list_t* subList = REDUCT_HANDLE_TO_LIST(valueHandle);
            for (uint32_t j = 0; j < subList->length; j++)
            {
                if (j * field->elementSize >= field->size)
                {
                    break;
                }
                reduct_schema_apply_primitive(reduct, field->subtype, field->elementSize,
                    (char*)target + j * field->elementSize, subList->handles[j]);
            }
        }
        else
        {
            reduct_schema_apply_primitive(reduct, field->type, field->size, target, valueHandle);
        }
    }
    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
    return true;
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

    if (id >= reduct->global->schema.count)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        REDUCT_ERROR_THROW(reduct, "invalid schema ID");
    }

    reduct_schema_internal_t* schema = reduct->global->schema.schemas[id];
    if (schema == NULL)
    {
        reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
        REDUCT_ERROR_THROW(reduct, "invalid schema ID");
    }

    reduct_list_t* list = reduct_list_new(reduct, schema->count);

    for (size_t i = 0; i < schema->count; i++)
    {
        const reduct_schema_t* field = &schema->fields[i];
        void* val = (char*)in + field->offset;

        reduct_list_t* pair = reduct_list_new(reduct, 2);
        pair->handles[0] = REDUCT_HANDLE_CREATE_STRING(reduct, field->key);

        if (field->type == REDUCT_SCHEMA_TYPE_ARRAY)
        {
            size_t count = field->size / field->elementSize;
            reduct_list_t* subList = reduct_list_new(reduct, count);
            for (size_t j = 0; j < count; j++)
            {
                subList->handles[j] = reduct_schema_serialize_primitive(reduct, field->subtype, field->elementSize,
                    (char*)val + j * field->elementSize);
            }
            pair->handles[1] = REDUCT_HANDLE_FROM_LIST(subList);
        }
        else
        {
            reduct_handle_t valueHandle = reduct_schema_serialize_primitive(reduct, field->type, field->size, val);
            pair->handles[1] = valueHandle;
        }

        list->handles[i] = REDUCT_HANDLE_FROM_LIST(pair);
    }

    reduct_rwmutex_read_unlock(&reduct->global->schema.mutex);
    return REDUCT_HANDLE_FROM_LIST(list);
}
