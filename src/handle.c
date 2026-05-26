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
    if (REDUCT_HANDLE_IS_NIL(handle))
    {
        return REDUCT_HANDLE_TYPE_NONE;
    }

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
    case REDUCT_ITEM_TYPE_ATOM_STACK:
    {
        return REDUCT_HANDLE_TYPE_ATOM_STACK;
    }
    case REDUCT_ITEM_TYPE_LIST_NODE:
    {
        return REDUCT_HANDLE_TYPE_LIST_NODE;
    }
    case REDUCT_ITEM_TYPE_RVSDG_NODE:
    {
        return REDUCT_HANDLE_TYPE_RVSDG_NODE;
    }
    case REDUCT_ITEM_TYPE_RVSDG_EDGE:
    {
        return REDUCT_HANDLE_TYPE_RVSDG_EDGE;
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
    case REDUCT_HANDLE_TYPE_ATOM_STACK:
        return "atom stack";
    case REDUCT_HANDLE_TYPE_LIST_NODE:
        return "list node";
    case REDUCT_HANDLE_TYPE_RVSDG_NODE:
        return "ir node";
    case REDUCT_HANDLE_TYPE_RVSDG_EDGE:
        return "ir edge";
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

REDUCT_API bool reduct_handle_is_equal(reduct_t* reduct, reduct_handle_t* a, reduct_handle_t* b)
{
    assert(reduct != NULL);
    assert(a != NULL);
    assert(b != NULL);

    if (a->_value == b->_value)
    {
        return true;
    }

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(*a) && REDUCT_HANDLE_IS_NUMBER_SHAPED(*b))
    {
        double da = reduct_handle_as_number(reduct, *a);
        double db = reduct_handle_as_number(reduct, *b);
        return da == db;
    }

    reduct_handle_ensure_item(reduct, a);
    reduct_handle_ensure_item(reduct, b);

    reduct_item_t* itemA = REDUCT_HANDLE_TO_ITEM(*a);
    reduct_item_t* itemB = REDUCT_HANDLE_TO_ITEM(*b);

    if (itemA == itemB)
    {
        return true;
    }

    if (itemA->type != itemB->type)
    {
        return false;
    }

    if (itemA->type == REDUCT_ITEM_TYPE_ATOM)
    {
        reduct_atom_t* atomA = &itemA->atom;
        reduct_atom_t* atomB = &itemB->atom;
        return (atomA->flags & REDUCT_ATOM_FLAG_QUOTED) == (atomB->flags & REDUCT_ATOM_FLAG_QUOTED) &&
            atomA->length == atomB->length && memcmp(atomA->string, atomB->string, atomA->length) == 0;
    }

    if (itemA->type == REDUCT_ITEM_TYPE_LIST)
    {
        reduct_list_t* listA = &itemA->list;
        reduct_list_t* listB = &itemB->list;

        if (listA->length != listB->length)
        {
            return false;
        }

        reduct_list_iter_t iterA = REDUCT_LIST_ITER(listA);
        reduct_list_iter_t iterB = REDUCT_LIST_ITER(listB);
        reduct_handle_t itemA, itemB;

        while (reduct_list_iter_next(&iterA, &itemA) && reduct_list_iter_next(&iterB, &itemB))
        {
            if (!reduct_handle_is_equal(reduct, &itemA, &itemB))
            {
                return false;
            }
        }

        return true;
    }

    return false;
}

typedef struct
{
    int group;
    double num;
    reduct_item_t* item;
} reduct_cmp_val_t;

static inline void reduct_handle_unpack(reduct_handle_t* handle, reduct_cmp_val_t* out)
{
    assert(handle != NULL);
    assert(out != NULL);

    if (REDUCT_HANDLE_IS_NUMBER(*handle))
    {
        out->group = 0;
        out->num = REDUCT_HANDLE_TO_NUMBER(*handle);
        out->item = NULL;
        return;
    }

    out->item = REDUCT_HANDLE_TO_ITEM(*handle);
    if (out->item == NULL)
    {
        out->group = 1;
        return;
    }

    if (out->item->type == REDUCT_ITEM_TYPE_LIST)
    {
        out->group = 2;
        return;
    }

    if (out->item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_number(&out->item->atom))
    {
        out->group = 0;
        out->num = reduct_atom_get_number(&out->item->atom);
        return;
    }

    out->group = 1;
}

REDUCT_API int64_t reduct_handle_compare(reduct_t* reduct, reduct_handle_t* a, reduct_handle_t* b)
{
    assert(reduct != NULL);
    assert(a != NULL);
    assert(b != NULL);

    if (a == b || a->_value == b->_value)
    {
        return 0;
    }

    reduct_cmp_val_t va, vb;
    reduct_handle_unpack(a, &va);
    reduct_handle_unpack(b, &vb);

    if (va.group != vb.group)
    {
        return va.group - vb.group;
    }

    if (va.group == 0)
    {
        if (va.num < vb.num)
        {
            return -1;
        }
        if (va.num > vb.num)
        {
            return 1;
        }
        return 0;
    }
    else if (va.group == 1)
    {
        reduct_atom_t* atomA = &va.item->atom;
        reduct_atom_t* atomB = &vb.item->atom;
        size_t lenA = atomA->length;
        size_t lenB = atomB->length;
        size_t minLen = lenA < lenB ? lenA : lenB;

        const char* strA = atomA->string;
        const char* strB = atomB->string;

        int cmp = memcmp(strA, strB, minLen);
        if (cmp != 0)
        {
            return cmp;
        }

        return (int64_t)lenA - (int64_t)lenB;
    }

    reduct_list_t* listA = va.item ? &va.item->list : NULL;
    reduct_list_t* listB = vb.item ? &vb.item->list : NULL;
    size_t lenA = listA ? listA->length : 0;
    size_t lenB = listB ? listB->length : 0;
    size_t minLen = lenA < lenB ? lenA : lenB;

    for (size_t i = 0; i < minLen; i++)
    {
        reduct_handle_t ha = reduct_list_nth(reduct, listA, i);
        reduct_handle_t hb = reduct_list_nth(reduct, listB, i);
        int64_t cmp = reduct_handle_compare(reduct, &ha, &hb);
        if (cmp != 0)
        {
            return cmp;
        }
    }

    return (int64_t)lenA - (int64_t)lenB;
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

REDUCT_API void reduct_handle_push(reduct_t* reduct, reduct_handle_t list, reduct_handle_t val)
{
    assert(reduct != NULL);

    if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(list)))
    {
        REDUCT_ERROR_THROW(reduct, "push: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STRING(list));
    }

    reduct_list_push(reduct, REDUCT_HANDLE_TO_LIST(list), val);
}

REDUCT_API reduct_handle_t reduct_handle_at(reduct_t* reduct, reduct_handle_t handle, size_t index)
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
        return reduct_list_nth(reduct, &item->list, index);
    case REDUCT_ITEM_TYPE_ATOM:
    {
        char c = item->atom.string[index];
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &c, 1));
    }
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
