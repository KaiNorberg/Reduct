#include "reduct/item.h"
#include "reduct/stringify.h"

REDUCT_API size_t reduct_stringify(reduct_t* reduct, reduct_handle_t* handle, char* buffer, size_t size)
{
    assert(reduct != NULL);
    assert(buffer != NULL || size == 0);

    if (handle == NULL)
    {
        return snprintf(buffer, size, "<null>");
    }

    if (*handle == REDUCT_HANDLE_NONE)
    {
        return snprintf(buffer, size, "<none>");
    }

    if (!REDUCT_HANDLE_IS_ITEM(handle))
    {
        if (REDUCT_HANDLE_IS_INT(handle))
        {
            return snprintf(buffer, size, "%lld", (long long)REDUCT_HANDLE_TO_INT(handle));
        }
        else if (REDUCT_HANDLE_IS_FLOAT(handle))
        {
            return snprintf(buffer, size, "%g", (double)REDUCT_HANDLE_TO_FLOAT(handle));
        }

        return snprintf(buffer, size, "<unknown>");
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
                return snprintf(buffer, size, "%lld", (long long)reduct_atom_get_int(atom));
            }
            else if (reduct_atom_is_float(atom))
            {
                return snprintf(buffer, size, "%g", (double)reduct_atom_get_float(atom));
            }
            else if (reduct_atom_is_native(reduct, atom))
            {
                const char* anonymous = "anonymous";

                return snprintf(buffer, size, "<native: %.*s>", atom->length != 0 ? (int)atom->length : (int)strlen(anonymous), atom->length != 0 ? atom->string : anonymous);
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

        reduct_handle_t child;
        REDUCT_LIST_FOR_EACH(&child, &item->list)
        {
            res = reduct_stringify(reduct, &child, size > written ? buffer + written : NULL,
                size > written ? size - written : 0);
            written += res;

            if (_iter.index + 1 < item->length)
            {
                res = snprintf(size > written ? buffer + written : NULL,
                    size > written ? size - written : 0, " ");
                written += res;
            }
        }

        res =
            snprintf(size > written ? buffer + written : NULL, size > written ? size - written : 0, ")");
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