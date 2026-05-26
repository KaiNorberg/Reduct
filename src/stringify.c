#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/list.h>
#include <reduct/stringify.h>

REDUCT_API size_t reduct_stringify(reduct_t* reduct, reduct_handle_t handle, char* buffer, size_t size)
{
    assert(reduct != NULL);
    assert(buffer != NULL || size == 0);

    if (REDUCT_HANDLE_IS_NIL(handle))
    {
        return snprintf(buffer, size, "<none>");
    }

    if (!REDUCT_HANDLE_IS_ITEM(handle))
    {
        if (REDUCT_HANDLE_IS_NUMBER(handle))
        {
            return snprintf(buffer, size, "%g", REDUCT_HANDLE_TO_NUMBER(handle));
        }

        return snprintf(buffer, size, "<unknown>");
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* atom = &item->atom;
        if (!(atom->flags & REDUCT_ATOM_FLAG_QUOTED))
        {
            if (reduct_atom_is_number(atom))
            {
                return snprintf(buffer, size, "%g", reduct_atom_get_number(atom));
            }
            else if (reduct_atom_is_native(reduct, atom))
            {
                const char* anonymous = "anonymous";

                return snprintf(buffer, size, "<native: %.*s>",
                    atom->length != 0 ? (int)atom->length : (int)strlen(anonymous),
                    atom->length != 0 ? atom->string : anonymous);
            }

            return snprintf(buffer, size, "%.*s", (int)atom->length, atom->string);
        }

        return snprintf(buffer, size, "\"%.*s\"", (int)atom->length, atom->string);
    }
    case REDUCT_ITEM_TYPE_LIST:
    {
        size_t written = 0;
        size_t res = snprintf(buffer, size, "(");
        written += res;

        reduct_list_iter_t iter = REDUCT_LIST_ITER(&item->list);
        reduct_list_chunk_t chunk;
        while (reduct_list_iter_next_chunk(&iter, &chunk))
        {
            size_t baseIdx = iter.index - chunk.count;
            for (size_t i = 0; i < chunk.count; i++)
            {
                reduct_handle_t child = chunk.handles[i];
                res = reduct_stringify(reduct, child, size > written ? buffer + written : NULL,
                    size > written ? size - written : 0);
                written += res;

                if (baseIdx + i + 1 < item->length)
                {
                    res = snprintf(size > written ? buffer + written : NULL, size > written ? size - written : 0, " ");
                    written += res;
                }
            }
        }

        res = snprintf(size > written ? buffer + written : NULL, size > written ? size - written : 0, ")");
        written += res;

        return written;
    }
    case REDUCT_ITEM_TYPE_FUNCTION:
        return snprintf(buffer, size, "<function: %p>", (void*)item);
    case REDUCT_ITEM_TYPE_CLOSURE:
        return snprintf(buffer, size, "<closure: %p>", (void*)item);
    default:
        return snprintf(buffer, size, "<unknown>");
    }
    return 0;
}

REDUCT_API char* reduct_stringify_alloc(reduct_t* reduct, reduct_handle_t handle)
{
    size_t len = reduct_stringify(reduct, handle, NULL, 0);
    char* buffer = (char*)malloc(len + 1);
    if (buffer == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        return NULL;
    }

    reduct_stringify(reduct, handle, buffer, len + 1);
    return buffer;
}
