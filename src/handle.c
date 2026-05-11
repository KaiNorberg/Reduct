#include "reduct/handle.h"
#include "reduct/atom.h"
#include "reduct/char.h"
#include "reduct/core.h"
#include "reduct/defs.h"
#include "reduct/eval.h"
#include "reduct/gc.h"
#include "reduct/item.h"
#include "reduct/stringify.h"

REDUCT_API void reduct_handle_ensure_item(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    assert(handle != NULL);

    if (REDUCT_HANDLE_IS_ITEM(handle))
    {
        return;
    }

    if (REDUCT_HANDLE_IS_INT(handle))
    {
        reduct_atom_t* atom = reduct_atom_new_int(reduct, REDUCT_HANDLE_TO_INT(handle));
        *handle = REDUCT_HANDLE_FROM_ATOM(atom);
        return;
    }

    reduct_atom_t* atom = reduct_atom_new_float(reduct, REDUCT_HANDLE_TO_FLOAT(handle));
    *handle = REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
}

REDUCT_API void reduct_handle_promote(struct reduct* reduct, reduct_handle_t* a, reduct_handle_t* b,
    reduct_promotion_t* out)
{
    assert(reduct != NULL);
    assert(a != NULL);
    assert(b != NULL);
    assert(out != NULL);

    if (REDUCT_HANDLE_IS_INT(a) && REDUCT_HANDLE_IS_INT(b))
    {
        out->type = REDUCT_PROMOTION_TYPE_INT;
        out->a.intVal = REDUCT_HANDLE_TO_INT(a);
        out->b.intVal = REDUCT_HANDLE_TO_INT(b);
        return;
    }

    if (REDUCT_HANDLE_IS_FLOAT(a) && REDUCT_HANDLE_IS_FLOAT(b))
    {
        out->type = REDUCT_PROMOTION_TYPE_FLOAT;
        out->a.floatVal = REDUCT_HANDLE_TO_FLOAT(a);
        out->b.floatVal = REDUCT_HANDLE_TO_FLOAT(b);
        return;
    }

    reduct_handle_ensure_item(reduct, a);
    reduct_handle_ensure_item(reduct, b);

    reduct_item_t* itemA = REDUCT_HANDLE_TO_ITEM(a);
    reduct_item_t* itemB = REDUCT_HANDLE_TO_ITEM(b);

    if (itemA->type != REDUCT_ITEM_TYPE_ATOM || itemB->type != REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_RUNTIME(reduct, "incompatible operand types %s and %s", reduct_item_type_str(itemA),
            reduct_item_type_str(itemB));
    }

    reduct_atom_t* atomA = &itemA->atom;
    reduct_atom_t* atomB = &itemB->atom;

    if (reduct_atom_is_float(atomA) || reduct_atom_is_float(atomB))
    {
        out->type = REDUCT_PROMOTION_TYPE_FLOAT;
        out->a.floatVal = reduct_atom_get_float(atomA);
        out->b.floatVal = reduct_atom_get_float(atomB);
    }
    else if (reduct_atom_is_int(atomA) && reduct_atom_is_int(atomB))
    {
        out->type = REDUCT_PROMOTION_TYPE_INT;
        out->a.intVal = reduct_atom_get_int(atomA);
        out->b.intVal = reduct_atom_get_int(atomB);
    }
    else
    {
        REDUCT_ERROR_RUNTIME(reduct, "incompatible operand types %s and %s", reduct_item_type_str(itemA),
            reduct_item_type_str(itemB));
    }
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

    if (REDUCT_HANDLE_IS_NUMBER_SHAPED(a) && REDUCT_HANDLE_IS_NUMBER_SHAPED(b))
    {
        reduct_promotion_t prom;
        reduct_handle_promote(reduct, a, b, &prom);
        if (prom.type == REDUCT_PROMOTION_TYPE_INT)
        {
            return prom.a.intVal == prom.b.intVal;
        }
        return prom.a.floatVal == prom.b.floatVal;
    }

    reduct_handle_ensure_item(reduct, a);
    reduct_handle_ensure_item(reduct, b);

    reduct_item_t* itemA = REDUCT_HANDLE_TO_ITEM(a);
    reduct_item_t* itemB = REDUCT_HANDLE_TO_ITEM(b);

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
    bool isFloat;
    union {
        int64_t i;
        double f;
    } num;
    reduct_item_t* item;
} reduct_cmp_val_t;

static inline void reduct_handle_unpack(reduct_handle_t* handle, reduct_cmp_val_t* out)
{
    assert(handle != NULL);
    assert(out != NULL);

    if (REDUCT_HANDLE_IS_INT(handle))
    {
        out->group = 0;
        out->isFloat = false;
        out->num.i = REDUCT_HANDLE_TO_INT(handle);
        out->item = NULL;
        return;
    }

    if (REDUCT_HANDLE_IS_FLOAT(handle))
    {
        out->group = 0;
        out->isFloat = true;
        out->num.f = REDUCT_HANDLE_TO_FLOAT(handle);
        out->item = NULL;
        return;
    }

    out->item = REDUCT_HANDLE_TO_ITEM(handle);
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

    if (out->item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_float(&out->item->atom))
    {
        out->group = 0;
        out->isFloat = true;
        out->num.f = reduct_atom_get_float(&out->item->atom);
        return;
    }

    if (out->item->type == REDUCT_ITEM_TYPE_ATOM && reduct_atom_is_int(&out->item->atom))
    {
        out->group = 0;
        out->isFloat = false;
        out->num.i = reduct_atom_get_int(&out->item->atom);
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
        if (!va.isFloat && !vb.isFloat)
        {
            return (va.num.i < vb.num.i) ? -1 : ((va.num.i > vb.num.i) ? 1 : 0);
        }
        else if (va.isFloat && vb.isFloat)
        {
            return (va.num.f < vb.num.f) ? -1 : ((va.num.f > vb.num.f) ? 1 : 0);
        }

        double fa = va.isFloat ? va.num.f : (double)va.num.i;
        double fb = vb.isFloat ? vb.num.f : (double)vb.num.i;
        if (fa < fb)
        {
            return -1;
        }
        if (fa > fb)
        {
            return 1;
        }
        return va.isFloat ? 1 : -1;
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
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    if (item->type != REDUCT_ITEM_TYPE_ATOM)
    {
        REDUCT_ERROR_RUNTIME(reduct, "expected atom, got %s", reduct_item_type_str(item));
    }
    *outStr = item->atom.string;
    *outLen = item->length;
}

REDUCT_API void reduct_handle_push(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t val)
{
    assert(reduct != NULL);
    assert(list != NULL);

    if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(list)))
    {
        REDUCT_ERROR_RUNTIME(reduct, "push: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list));
    }

    reduct_list_push(reduct, REDUCT_HANDLE_TO_LIST(list), val);
}

REDUCT_API reduct_handle_t reduct_handle_at(reduct_t* reduct, reduct_handle_t* handle, size_t index)
{
    assert(reduct != NULL);
    assert(handle != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (REDUCT_UNLIKELY(index >= item->length))
    {
        REDUCT_ERROR_RUNTIME(reduct, "index %zu out of bounds for %s of length %u", index, reduct_item_type_str(item),
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
        REDUCT_ERROR_RUNTIME(reduct, "expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API size_t reduct_handle_len(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    assert(handle != NULL);

    return reduct_handle_as_item(reduct, handle)->length;
}

REDUCT_API bool reduct_handle_is_str(reduct_t* reduct, reduct_handle_t* handle, const char* str)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);
    assert(handle != NULL);
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
