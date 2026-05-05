#include "reduct/item.h"
#include "reduct/stringify.h"

static inline reduct_size_t reduct_stringify_internal(reduct_t* reduct, reduct_handle_t* handle, char* buffer,
    reduct_size_t size)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(buffer != REDUCT_NULL || size == 0);

    if (handle == REDUCT_NULL)
    {
        return REDUCT_SNPRINTF(buffer, size, "<null>");
    }

    if (*handle == REDUCT_HANDLE_NONE)
    {
        return REDUCT_SNPRINTF(buffer, size, "<none>");
    }

    if (!REDUCT_HANDLE_IS_ITEM(handle))
    {
        if (REDUCT_HANDLE_IS_INT(handle))
        {
            return REDUCT_SNPRINTF(buffer, size, "%lld", (long long)REDUCT_HANDLE_TO_INT(handle));
        }
        else if (REDUCT_HANDLE_IS_FLOAT(handle))
        {
            return REDUCT_SNPRINTF(buffer, size, "%g", (double)REDUCT_HANDLE_TO_FLOAT(handle));
        }

        return REDUCT_SNPRINTF(buffer, size, "<unknown>");
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* atom = &item->atom;
        if (!(item->flags & REDUCT_ITEM_FLAG_QUOTED))
        {
            if (reduct_atom_is_int(atom))
            {
                return REDUCT_SNPRINTF(buffer, size, "%lld", (long long)reduct_atom_get_int(atom));
            }
            else if (reduct_atom_is_float(atom))
            {
                return REDUCT_SNPRINTF(buffer, size, "%g", (double)reduct_atom_get_float(atom));
            }

            return REDUCT_SNPRINTF(buffer, size, "%.*s", (int)atom->length, atom->string);
        }

        return REDUCT_SNPRINTF(buffer, size, "\"%.*s\"", (int)atom->length, atom->string);
    }
    case REDUCT_ITEM_TYPE_LIST:
    {
        reduct_size_t written = 0;
        reduct_size_t res = REDUCT_SNPRINTF(buffer, size, "(");
        written += res;

        reduct_handle_t child;
        REDUCT_LIST_FOR_EACH(&child, &item->list)
        {
            res = reduct_stringify(reduct, &child, size > written ? buffer + written : REDUCT_NULL,
                size > written ? size - written : 0);
            written += res;

            if (_iter.index + 1 < item->length)
            {
                res = REDUCT_SNPRINTF(size > written ? buffer + written : REDUCT_NULL,
                    size > written ? size - written : 0, " ");
                written += res;
            }
        }

        res =
            REDUCT_SNPRINTF(size > written ? buffer + written : REDUCT_NULL, size > written ? size - written : 0, ")");
        written += res;

        return written;
    }
    case REDUCT_ITEM_TYPE_FUNCTION:
        return REDUCT_SNPRINTF(buffer, size, "<function at %p>", (void*)item);
    case REDUCT_ITEM_TYPE_CLOSURE:
        return REDUCT_SNPRINTF(buffer, size, "<closure at %p>", (void*)item);
    default:
        return REDUCT_SNPRINTF(buffer, size, "<unknown>");
    }
    return 0;
}

REDUCT_API reduct_size_t reduct_stringify(reduct_t* reduct, reduct_handle_t* handle, char* buffer, reduct_size_t size)
{
    if (REDUCT_HANDLE_IS_ATOM(handle))
    {
        const char* str;
        reduct_size_t len;
        reduct_handle_atom_string(reduct, handle, &str, &len);
        if (buffer != REDUCT_NULL && size > 0 && len != 0)
        {
            if (REDUCT_HANDLE_TO_ITEM(handle)->flags & REDUCT_ITEM_FLAG_QUOTED)
            {
                return REDUCT_SNPRINTF(buffer, size, "\"%.*s\"", (int)len, str);
            }

            reduct_size_t copyLen = (len < size) ? len : size - 1;
            REDUCT_MEMCPY(buffer, str, copyLen);
            buffer[copyLen] = '\0';
        }
        return len;
    }

    return reduct_stringify_internal(reduct, handle, buffer, size);
}