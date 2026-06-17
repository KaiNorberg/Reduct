#include <reduct/atom.h>
#include <reduct/char.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/stringify.h>

REDUCT_API reduct_handle_type_t reduct_handle_get_type(reduct_handle_t handle)
{
    if (REDUCT_HANDLE_IS_NUMBER(handle))
    {
        return REDUCT_HANDLE_TYPE_NUMBER;
    }

    if (!REDUCT_HANDLE_IS_ITEM(handle))
    {
        return REDUCT_HANDLE_TYPE_UNKNOWN;
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_NONE:
    {
        return REDUCT_HANDLE_TYPE_NONE;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        if (reduct_atom_is_number(&item->atom))
        {
            return REDUCT_HANDLE_TYPE_NUMBER;
        }
        return REDUCT_HANDLE_TYPE_ATOM;
    }
    case REDUCT_ITEM_TYPE_LIST:
    {
        return REDUCT_HANDLE_TYPE_LIST;
    }
    case REDUCT_ITEM_TYPE_FUNCTION:
    {
        return REDUCT_HANDLE_TYPE_FUNCTION;
    }
    case REDUCT_ITEM_TYPE_CLOSURE:
    {
        return REDUCT_HANDLE_TYPE_CLOSURE;
    }
    case REDUCT_ITEM_TYPE_ARENA:
    {
        return REDUCT_HANDLE_TYPE_ARENA;
    }
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
    {
        return REDUCT_HANDLE_TYPE_RVSDG_NODE;
    }
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
    {
        return REDUCT_HANDLE_TYPE_RVSDG_EDGE;
    }
    case REDUCT_ITEM_TYPE_FUTURE:
    {
        return REDUCT_HANDLE_TYPE_FUTURE;
    }
    default:
    {
        return REDUCT_HANDLE_TYPE_UNKNOWN;
    }
    }
}

REDUCT_API const char* reduct_handle_type_string(reduct_handle_type_t type)
{
    switch (type)
    {
    case REDUCT_HANDLE_TYPE_NONE:
        return "none";
    case REDUCT_HANDLE_TYPE_NUMBER:
        return "number";
    case REDUCT_HANDLE_TYPE_ATOM:
        return "atom";
    case REDUCT_HANDLE_TYPE_LIST:
        return "list";
    case REDUCT_HANDLE_TYPE_FUNCTION:
        return "function";
    case REDUCT_HANDLE_TYPE_CLOSURE:
        return "closure";
    case REDUCT_HANDLE_TYPE_ARENA:
        return "arena";
    case REDUCT_HANDLE_TYPE_RVSDG_NODE:
        return "ir node";
    case REDUCT_HANDLE_TYPE_RVSDG_EDGE:
        return "ir edge";
    case REDUCT_HANDLE_TYPE_FUTURE:
        return "future";
    default:
        return "unknown";
    }
}

REDUCT_API void reduct_handle_ensure_item(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    assert(handle != NULL);

    if (REDUCT_HANDLE_IS_ITEM(*handle))
    {
        return;
    }

    reduct_atom_t* atom = reduct_atom_new_number(reduct, REDUCT_HANDLE_TO_NUMBER(*handle));
    *handle = REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
}

REDUCT_API bool reduct_handle_is_equal(reduct_t* reduct, reduct_handle_t a, reduct_handle_t b)
{
    assert(reduct != NULL);

    if (a._value == b._value)
    {
        return true;
    }

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(a) && REDUCT_HANDLE_IS_NUMBER_SHAPED(b))
    {
        double da = reduct_handle_as_number(reduct, a);
        double db = reduct_handle_as_number(reduct, b);
        return da == db;
    }

    if (!REDUCT_HANDLE_IS_ITEM(a) || !REDUCT_HANDLE_IS_ITEM(b))
    {
        return false;
    }

    reduct_item_t* itemA = REDUCT_HANDLE_TO_ITEM(a);
    reduct_item_t* itemB = REDUCT_HANDLE_TO_ITEM(b);

    if (itemA->type != itemB->type)
    {
        return false;
    }

    if (itemA->type == REDUCT_ITEM_TYPE_ATOM)
    {
        reduct_atom_t* atomA = &itemA->atom;
        reduct_atom_t* atomB = &itemB->atom;
        return reduct_atom_ensure_interned(reduct, atomA) == reduct_atom_ensure_interned(reduct, atomB);
    }

    if (itemA->type == REDUCT_ITEM_TYPE_LIST)
    {
        reduct_list_t* listA = &itemA->list;
        reduct_list_t* listB = &itemB->list;

        if (listA->length != listB->length)
        {
            return false;
        }

        for (uint32_t i = 0; i < listA->length; i++)
        {
            if (!reduct_handle_is_equal(reduct, listA->handles[i], listB->handles[i]))
            {
                return false;
            }
        }

        return true;
    }

    return false;
}

REDUCT_API int64_t reduct_handle_compare(reduct_t* reduct, reduct_handle_t a, reduct_handle_t b)
{
    assert(reduct != NULL);

    if (a._value == b._value)
    {
        return 0;
    }

    reduct_handle_type_t typeA = reduct_handle_get_type(a);
    reduct_handle_type_t typeB = reduct_handle_get_type(b);

    if (typeA != typeB)
    {
        return (int64_t)typeA - (int64_t)typeB;
    }

    switch (typeA)
    {
    case REDUCT_HANDLE_TYPE_NUMBER:
    {
        double da = reduct_handle_as_number(reduct, a);
        double db = reduct_handle_as_number(reduct, b);
        return (da > db) - (da < db);
    }
    case REDUCT_HANDLE_TYPE_ATOM:
    {
        reduct_atom_t* atomA = REDUCT_HANDLE_TO_ATOM(a);
        reduct_atom_t* atomB = REDUCT_HANDLE_TO_ATOM(b);
        uint32_t minLen = REDUCT_MIN(atomA->length, atomB->length);
        int res = memcmp(atomA->string, atomB->string, minLen);
        if (res != 0)
        {
            return (int64_t)res;
        }
        return (int64_t)atomA->length - (int64_t)atomB->length;
    }
    case REDUCT_HANDLE_TYPE_LIST:
    {
        reduct_list_t* listA = REDUCT_HANDLE_TO_LIST(a);
        reduct_list_t* listB = REDUCT_HANDLE_TO_LIST(b);
        uint32_t minLen = REDUCT_MIN(listA->length, listB->length);
        for (uint32_t i = 0; i < minLen; i++)
        {
            int64_t res = reduct_handle_compare(reduct, listA->handles[i], listB->handles[i]);
            if (res != 0)
            {
                return res;
            }
        }
        return (int64_t)listA->length - (int64_t)listB->length;
    }
    default:
    {
        return (a._value > b._value) - (a._value < b._value);
    }
    }
}

REDUCT_API void reduct_handle_atom_string(reduct_t* reduct, reduct_handle_t* handle, const char** outStr,
    size_t* outLen)
{
    reduct_handle_ensure_item(reduct, handle);
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(*handle);
    if (item->type != REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_THROW(reduct, "expected atom, got %s", reduct_item_type_str(item));
    }
    *outStr = item->atom.string;
    *outLen = item->length;
}

REDUCT_API reduct_handle_t reduct_handle_nth(reduct_t* reduct, reduct_handle_t handle, size_t index)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (REDUCT_UNLIKELY(index >= item->length))
    {
        REDUCT_ERROR_THROW(reduct, "index %zu out of bounds for %s of length %u", index, reduct_item_type_str(item),
            item->length);
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
        return item->list.handles[index];
    case REDUCT_ITEM_TYPE_ATOM:
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &item->atom.string[index], 1));
    default:
        REDUCT_ERROR_THROW(reduct, "expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API size_t reduct_handle_len(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    return reduct_handle_as_item(reduct, handle)->length;
}

REDUCT_API bool reduct_handle_is_str(reduct_t* reduct, reduct_handle_t handle, const char* str)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);
    assert(str != NULL);

    if (!REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        return false;
    }

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    if (item->type != REDUCT_ITEM_TYPE_ATOM)
    {
        return false;
    }

    return reduct_atom_is_equal(&item->atom, str, strlen(str));
}
