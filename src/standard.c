#include "reduct/standard.h"
#include "reduct/atom.h"
#include "reduct/char.h"
#include "reduct/compile.h"
#include "reduct/core.h"
#include "reduct/defs.h"
#include "reduct/eval.h"
#include "reduct/gc.h"
#include "reduct/handle.h"
#include "reduct/item.h"
#include "reduct/native.h"
#include "reduct/parse.h"
#include "reduct/stringify.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

REDUCT_API reduct_handle_t reduct_assert(reduct_t* reduct, reduct_handle_t* cond, reduct_handle_t* msg)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_TRUTHY(cond))
    {
        const char* str;
        size_t len;
        reduct_handle_atom_string(reduct, msg, &str, &len);
        REDUCT_ERROR_RUNTIME(reduct, "%.*s", (int)len, str);
    }

    return *cond;
}

REDUCT_API reduct_handle_t reduct_throw(reduct_t* reduct, reduct_handle_t* msg)
{
    assert(reduct != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, msg, &str, &len);
    REDUCT_ERROR_RUNTIME(reduct, "%.*s", len, str);

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_try(reduct_t* reduct, reduct_handle_t* callable, reduct_handle_t* catchFn)
{
    assert(reduct != NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "try: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(callable));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, catchFn), "try: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(catchFn));

    volatile reduct_error_t* prev = reduct->error;
    volatile size_t savedFrameCount = reduct->frameCount;
    volatile size_t savedRegCount = reduct->regCount;

    reduct_error_t error = REDUCT_ERROR();
    error.reduct = reduct;
    reduct->error = &error;

    if (REDUCT_ERROR_CATCH(&error))
    {
        reduct->error = (reduct_error_t*)prev;
        reduct->frameCount = savedFrameCount;
        reduct->regCount = savedRegCount;

        reduct_handle_t msg =
            REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, error.message, strlen(error.message)));
        return reduct_eval_call(reduct, *catchFn, 1, &msg);
    }

    reduct_handle_t result = reduct_eval_call(reduct, *callable, 0, NULL);
    reduct->error = (reduct_error_t*)prev;
    return result;
}

REDUCT_API reduct_handle_t reduct_map(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "map: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "map: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);

    reduct_list_t* mappedList = reduct_list_new(reduct);
    reduct_handle_t mappedHandle = REDUCT_HANDLE_FROM_LIST(mappedList);

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &listItem->list)
    {
        reduct_handle_t result = reduct_eval_call(reduct, *callable, 1, &entry);
        reduct_list_push(reduct, mappedList, result);
    }

    return mappedHandle;
}

REDUCT_API reduct_handle_t reduct_filter(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "filter: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable),
        "filter: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);

    reduct_list_t* filteredList = reduct_list_new(reduct);
    reduct_handle_t filteredHandle = REDUCT_HANDLE_FROM_LIST(filteredList);

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &listItem->list)
    {
        reduct_handle_t result = reduct_eval_call(reduct, *callable, 1, &entry);
        if (REDUCT_HANDLE_IS_TRUTHY(&result))
        {
            reduct_list_push(reduct, filteredList, entry);
        }
    }

    return filteredHandle;
}

REDUCT_API reduct_handle_t reduct_reduce(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* initial,
    reduct_handle_t* callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "reduce: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable),
        "reduce: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);
    reduct_handle_t accumulator = (initial != NULL) ? *initial : REDUCT_HANDLE_NONE;

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &listItem->list)
    {
        if (REDUCT_HANDLE_IS_NONE(&accumulator))
        {
            accumulator = entry;
            continue;
        }

        reduct_handle_t args[2] = {accumulator, entry};
        reduct_handle_t result = reduct_eval_call(reduct, *callable, 2, args);

        accumulator = result;
    }

    if (REDUCT_HANDLE_IS_NONE(&accumulator))
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    return accumulator;
}

REDUCT_API reduct_handle_t reduct_apply(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "apply: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "apply: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);
    size_t len = listItem->length;
    if (len == 0)
    {
        return reduct_eval_call(reduct, *callable, 0, NULL);
    }

    REDUCT_SCRATCH(reduct, argv, reduct_handle_t, len);

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &listItem->list)
    {
        argv[_iter.index] = entry;
    }

    reduct_handle_t result = reduct_eval_call(reduct, *callable, len, argv);

    REDUCT_SCRATCH_FREE(reduct, argv);

    return result;
}

static inline reduct_handle_t reduct_eval_maybe_call(reduct_t* reduct, reduct_handle_t fn, reduct_handle_t* arg)
{
    if (REDUCT_HANDLE_IS_NONE(&fn))
    {
        return *arg;
    }
    else
    {
        return reduct_eval_call(reduct, fn, 1, arg);
    }
}

#define REDUCT_ANY_ALL_IMPL(_name, _predicate, _default) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable) \
    { \
        assert(reduct != NULL); \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), #_name ": expected list, got %s", \
            REDUCT_HANDLE_GET_TYPE_STR(list)); \
        reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list); \
        reduct_handle_t fn = (callable != NULL) ? *callable : REDUCT_HANDLE_NONE; \
        reduct_handle_t entry; \
        REDUCT_LIST_FOR_EACH(&entry, &listItem->list) \
        { \
            reduct_handle_t result = reduct_eval_maybe_call(reduct, fn, &entry); \
            if (_predicate) \
            { \
                return REDUCT_HANDLE_FROM_INT(!(_default)); \
            } \
        } \
        return REDUCT_HANDLE_FROM_INT(_default); \
    }

REDUCT_ANY_ALL_IMPL(reduct_any, REDUCT_HANDLE_IS_TRUTHY(&result), false)
REDUCT_ANY_ALL_IMPL(reduct_all, !REDUCT_HANDLE_IS_TRUTHY(&result), true)

static void reduct_sort_merge(reduct_t* reduct, reduct_handle_t callable, reduct_handle_t* a, size_t left, size_t right,
    size_t end, reduct_handle_t* b)
{
    size_t i = left;
    size_t j = right;

    for (size_t k = left; k < end; k++)
    {
        bool useLeft = false;
        if (i < right)
        {
            if (j >= end)
            {
                useLeft = true;
            }
            else
            {
                if (!REDUCT_HANDLE_IS_NONE(&callable))
                {
                    reduct_handle_t args[2] = {a[i], a[j]};
                    reduct_handle_t res = reduct_eval_call(reduct, callable, 2, args);
                    if (REDUCT_HANDLE_IS_TRUTHY(&res))
                    {
                        useLeft = true;
                    }
                }
                else
                {
                    if (reduct_handle_compare(reduct, &a[i], &a[j]) <= 0)
                    {
                        useLeft = true;
                    }
                }
            }
        }

        if (useLeft)
        {
            b[k] = a[i];
            i++;
        }
        else
        {
            b[k] = a[j];
            j++;
        }
    }
}

REDUCT_API reduct_handle_t reduct_sort(reduct_t* reduct, reduct_handle_t* listHandle, reduct_handle_t* callableHandle)
{
    assert(reduct != NULL);
    assert(listHandle != NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "sort: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(listHandle));
    reduct_item_t* list = REDUCT_HANDLE_TO_ITEM(listHandle);

    reduct_handle_t callable = (callableHandle != NULL) ? *callableHandle : REDUCT_HANDLE_NONE;
    if (!REDUCT_HANDLE_IS_NONE(&callable))
    {
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, &callable),
            "sort: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(&callable));
    }

    size_t len = list->length;
    if (len <= 1)
    {
        return *listHandle;
    }

    reduct_handle_t* a = (reduct_handle_t*)malloc(len * sizeof(reduct_handle_t));
    if (a == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    reduct_handle_t* b = (reduct_handle_t*)malloc(len * sizeof(reduct_handle_t));
    if (b == NULL)
    {
        free(a);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &list->list)
    {
        a[_iter.index] = entry;
    }

    reduct_handle_t* src = a;
    reduct_handle_t* dst = b;
    for (size_t width = 1; width < list->length; width *= 2)
    {
        for (size_t i = 0; i < list->length; i += 2 * width)
        {
            size_t left = i;
            size_t right = REDUCT_MIN(i + width, len);
            size_t end = REDUCT_MIN(i + 2 * width, len);
            reduct_sort_merge(reduct, callable, src, left, right, end, dst);
        }
        reduct_handle_t* temp = src;
        src = dst;
        dst = temp;
    }

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    for (size_t i = 0; i < len; i++)
    {
        reduct_list_push(reduct, resultList, src[i]);
    }

    free(a);
    free(b);
    return resultHandle;
}

static inline int64_t reduct_handle_normalize_index(reduct_t* reduct, reduct_handle_t* index, size_t length)
{
    reduct_handle_t nHandle = reduct_get_int(reduct, index);
    int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);
    if (n < 0)
    {
        n = (int64_t)length + n;
    }
    return n;
}

static inline void reduct_sequence_normalize_range(reduct_t* reduct, reduct_handle_t* startH, reduct_handle_t* endH,
    size_t length, size_t* outStart, size_t* outEnd)
{
    int64_t start = reduct_handle_normalize_index(reduct, startH, length);
    int64_t end;

    if (endH != NULL)
    {
        end = reduct_handle_normalize_index(reduct, endH, length);
    }
    else
    {
        end = (int64_t)length;
    }

    start = REDUCT_MAX(0, REDUCT_MIN(start, (int64_t)length));
    end = REDUCT_MAX(0, REDUCT_MIN(end, (int64_t)length));

    *outStart = (size_t)start;
    *outEnd = (size_t)end;
}

REDUCT_API reduct_handle_t reduct_len(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    size_t total = 0;
    for (size_t i = 0; i < argc; i++)
    {
        total += reduct_handle_as_item(reduct, &argv[i])->length;
    }
    return REDUCT_HANDLE_FROM_INT(total);
}

REDUCT_API reduct_handle_t reduct_range(struct reduct* reduct, reduct_handle_t* start, reduct_handle_t* end,
    reduct_handle_t* step)
{
    assert(reduct != NULL);

    reduct_handle_t startH = reduct_get_int(reduct, start);
    reduct_handle_t endH = reduct_get_int(reduct, end);
    reduct_handle_t stepH = (step != NULL) ? reduct_get_int(reduct, step) : REDUCT_HANDLE_FROM_INT(1);

    int64_t startVal = REDUCT_HANDLE_TO_INT(&startH);
    int64_t endVal = REDUCT_HANDLE_TO_INT(&endH);
    int64_t stepVal = REDUCT_HANDLE_TO_INT(&stepH);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, stepVal != 0, "range: step must not be zero");

    size_t count = 0;
    if (stepVal > 0)
    {
        if (endVal > startVal)
        {
            count = (size_t)((endVal - startVal + stepVal - 1) / stepVal);
        }
    }
    else
    {
        if (startVal > endVal)
        {
            count = (size_t)((startVal - endVal - stepVal - 1) / -stepVal);
        }
    }

    reduct_list_t* list = reduct_list_new(reduct);
    reduct_handle_t listHandle = REDUCT_HANDLE_FROM_LIST(list);

    int64_t current = startVal;
    for (size_t i = 0; i < count; i++)
    {
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_INT(current));
        current += stepVal;
    }

    return listHandle;
}

REDUCT_API reduct_handle_t reduct_concat(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    bool resultIsList = false;

    for (size_t i = 0; i < argc; i++)
    {
        if (REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            resultIsList = true;
            continue;
        }
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM_LIKE(&argv[i]),
            "concat: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(&argv[i]));
    }

    if (resultIsList)
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

        if (argc == 0)
        {
            return newHandle;
        }

        for (size_t i = 0; i < argc; i++)
        {
            reduct_list_push(reduct, newList, argv[i]);
        }

        return newHandle;
    }

    size_t totalLen = 0;
    for (size_t i = 0; i < argc; i++)
    {
        totalLen += reduct_handle_as_atom(reduct, &argv[i])->length;
    }

    if (totalLen == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* first = reduct_handle_as_atom(reduct, &argv[0]);
    reduct_atom_t* result = reduct_atom_superstr(reduct, first, totalLen);
    char* dst = result->string + first->length;
    for (size_t i = 1; i < argc; i++)
    {
        reduct_atom_t* src = reduct_handle_as_atom(reduct, &argv[i]);
        memcpy(dst, src->string, src->length);
        dst += src->length;
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_append(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(&argv[0]), "append: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(&argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }
    reduct_list_t* list = &reduct_handle_as_item(reduct, &argv[0])->list;

    reduct_list_t* newList = reduct_list_append(reduct, list, argv[1]);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (size_t i = 2; i < argc; i++)
    {
        reduct_list_push(reduct, newList, argv[i]);
    }

    return newHandle;
}

REDUCT_API reduct_handle_t reduct_prepend(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(&argv[0]), "prepend: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(&argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }

    reduct_list_t* list = &reduct_handle_as_item(reduct, &argv[0])->list;
    reduct_list_t* newList = reduct_list_prepend(reduct, list, argv[1]);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (size_t i = 2; i < argc; i++)
    {
        reduct_list_push(reduct, newList, argv[i]);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_edge(reduct_t* reduct, reduct_handle_t* handle, bool first)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    size_t index = first ? 0 : item->length - 1;

    if (item->type == REDUCT_ITEM_TYPE_LIST)
    {
        return reduct_list_nth(reduct, &item->list, index);
    }
    else
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, index, 1));
    }
}

REDUCT_API reduct_handle_t reduct_first(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "first: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_edge(reduct, handle, true);
}

REDUCT_API reduct_handle_t reduct_last(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "last: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_edge(reduct, handle, false);
}

static inline reduct_handle_t reduct_sequence_trim(reduct_t* reduct, reduct_handle_t* handle, bool rest)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length <= 1)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    size_t start = rest ? 1 : 0;
    size_t end = rest ? item->length : item->length - 1;

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        return REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &item->list, start, end));
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, start, end - start));
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "trim: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_rest(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "rest: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_trim(reduct, handle, true);
}

REDUCT_API reduct_handle_t reduct_init(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "init: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_trim(reduct, handle, false);
}

REDUCT_API reduct_handle_t reduct_nth(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* index,
    reduct_handle_t* defaultVal)
{
    assert(reduct != NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "nth: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);

    if (n < 0 || n >= (int64_t)item->length)
    {
        return (defaultVal != NULL) ? *defaultVal : REDUCT_HANDLE_NIL(reduct);
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
        return reduct_list_nth(reduct, &item->list, (size_t)n);
    case REDUCT_ITEM_TYPE_ATOM:
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, (size_t)n, 1));
    default:
        return (defaultVal != NULL) ? *defaultVal : REDUCT_HANDLE_NIL(reduct);
    }
}

REDUCT_API reduct_handle_t reduct_assoc(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* index,
    reduct_handle_t* value, reduct_handle_t* fillVal)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);
    if (n < 0)
    {
        n = 0;
    }

    size_t targetIndex = (size_t)n;

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, targetIndex < item->length || fillVal != NULL, "assoc: index %zu out of bounds",
        targetIndex);

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        if (targetIndex < item->length)
        {
            reduct_list_t* newList = reduct_list_assoc(reduct, &item->list, targetIndex, *value);
            return REDUCT_HANDLE_FROM_LIST(newList);
        }

        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newListH = REDUCT_HANDLE_FROM_LIST(newList);
        reduct_list_push_list(reduct, newList, &item->list);
        for (size_t i = item->length; i < targetIndex; i++)
        {
            reduct_list_push(reduct, newList, *fillVal);
        }
        reduct_list_push(reduct, newList, *value);
        return newListH;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* src = &item->atom;
        reduct_atom_t* fill =
            (targetIndex > item->length && fillVal != NULL) ? &reduct_handle_as_item(reduct, fillVal)->atom : NULL;
        reduct_atom_t* val = &reduct_handle_as_item(reduct, value)->atom;

        size_t prefixLen = REDUCT_MIN(item->length, targetIndex);
        size_t fillCount = (targetIndex > item->length) ? targetIndex - item->length : 0;
        size_t suffixStart = targetIndex + 1;
        size_t suffixLen = (suffixStart < item->length) ? item->length - suffixStart : 0;

        size_t resultLen = prefixLen + val->length + suffixLen;
        if (fillCount > 0)
        {
            resultLen += fillCount * fill->length;
        }

        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        if (prefixLen > 0)
        {
            memcpy(dst, src->string, prefixLen);
            dst += prefixLen;
        }

        for (size_t i = 0; i < fillCount; i++)
        {
            memcpy(dst, fill->string, fill->length);
            dst += fill->length;
        }

        memcpy(dst, val->string, val->length);
        dst += val->length;

        if (suffixLen > 0)
        {
            memcpy(dst, src->string + suffixStart, suffixLen);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "assoc: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_dissoc(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* index)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);

    if (n < 0 || n >= (int64_t)item->length)
    {
        return *handle;
    }

    size_t targetIndex = (size_t)n;

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        reduct_list_t* newList = reduct_list_dissoc(reduct, &item->list, targetIndex);
        return REDUCT_HANDLE_FROM_LIST(newList);
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* src = &item->atom;

        size_t prefixLen = targetIndex;
        size_t suffixStart = targetIndex + 1;
        size_t suffixLen = (suffixStart < item->length) ? item->length - suffixStart : 0;
        size_t resultLen = prefixLen + suffixLen;

        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        if (prefixLen > 0)
        {
            memcpy(dst, src->string, prefixLen);
            dst += prefixLen;
        }
        if (suffixLen > 0)
        {
            memcpy(dst, src->string + suffixStart, suffixLen);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "dissoc: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_update(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* index,
    reduct_handle_t* callable, reduct_handle_t* fillVal)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t targetIndex = reduct_handle_normalize_index(reduct, index, item->length);
    if (targetIndex < 0)
    {
        targetIndex = 0;
    }

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, (size_t)targetIndex < item->length || fillVal != NULL,
        "update: index %lld out of bounds", (long long)targetIndex);

    reduct_handle_t currentVal = reduct_nth(reduct, handle, index, fillVal);
    reduct_handle_t newVal = reduct_eval_call(reduct, *callable, 1, &currentVal);

    return reduct_assoc(reduct, handle, index, &newVal, fillVal);
}

REDUCT_API reduct_handle_t reduct_index_of(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* target)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        reduct_handle_t current;
        REDUCT_LIST_FOR_EACH(&current, &item->list)
        {
            if (reduct_handle_compare(reduct, &current, target) == 0)
            {
                return REDUCT_HANDLE_FROM_INT(_iter.index);
            }
        }
    }
    break;
    case REDUCT_ITEM_TYPE_ATOM:
    {
        const char* targetStr;
        size_t targetLen;
        reduct_handle_atom_string(reduct, target, &targetStr, &targetLen);

        if (targetLen == 0)
        {
            return REDUCT_HANDLE_FROM_INT(0);
        }

        if (targetLen <= item->length)
        {
            const char* str = item->atom.string;
            for (size_t i = 0; i <= item->length - targetLen; i++)
            {
                if (memcmp(str + i, targetStr, targetLen) == 0)
                {
                    return REDUCT_HANDLE_FROM_INT(i);
                }
            }
        }
        break;
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "index-of: expected list or atom, got %s", reduct_item_type_str(item));
    }

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_reverse(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length <= 1)
    {
        return *handle;
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

        size_t i = item->length;
        while (i > 0)
        {
            reduct_list_push(reduct, newList, reduct_list_nth(reduct, &item->list, --i));
        }

        return newHandle;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* result = reduct_atom_new(reduct, item->length);
        const char* src = item->atom.string;
        char* dst = result->string;

        for (size_t i = 0; i < item->length; i++)
        {
            dst[i] = src[item->length - 1 - i];
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "reverse: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_slice(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* startH,
    reduct_handle_t* endH)
{
    assert(reduct != NULL);
    assert(handle != NULL);
    assert(startH != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    size_t start, end;
    reduct_sequence_normalize_range(reduct, startH, endH, item->length, &start, &end);

    if (start >= end)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        return REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &item->list, start, end));
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, start, end - start));
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "slice: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_flatten(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* depthH)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return *handle;
    }

    int64_t depth = -1;
    if (depthH != NULL)
    {
        reduct_handle_t d = reduct_get_int(reduct, depthH);
        depth = REDUCT_HANDLE_TO_INT(&d);
    }

    if (depth == 0)
    {
        return *handle;
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    reduct_handle_t current;
    REDUCT_LIST_FOR_EACH(&current, &item->list)
    {
        if (REDUCT_HANDLE_IS_LIST(&current))
        {
            reduct_handle_t nextDepthH = REDUCT_HANDLE_FROM_INT(depth - 1);
            reduct_handle_t flattened = reduct_flatten(reduct, &current, &nextDepthH);

            if (REDUCT_HANDLE_IS_LIST(&flattened))
            {
                reduct_list_push_list(reduct, newList, REDUCT_HANDLE_TO_LIST(&flattened));
            }
            else if (!REDUCT_HANDLE_IS_NIL(&flattened))
            {
                reduct_list_push(reduct, newList, flattened);
            }
        }
        else
        {
            reduct_list_push(reduct, newList, current);
        }
    }

    return newHandle;
}

REDUCT_API reduct_handle_t reduct_contains(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* target)
{
    assert(reduct != NULL);
    reduct_handle_t index = reduct_index_of(reduct, handle, target);
    return REDUCT_HANDLE_FROM_BOOL(!REDUCT_HANDLE_IS_NIL(&index));
}

REDUCT_API reduct_handle_t reduct_replace(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* oldVal,
    reduct_handle_t* newVal)
{
    assert(reduct != NULL);
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

        reduct_handle_t entry;
        REDUCT_LIST_FOR_EACH(&entry, &item->list)
        {
            if (reduct_handle_compare(reduct, &entry, oldVal) == 0)
            {
                reduct_list_push(reduct, newList, *newVal);
            }
            else
            {
                reduct_list_push(reduct, newList, entry);
            }
        }

        return newHandle;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        const char* oldStr;
        size_t oldLen;
        reduct_handle_atom_string(reduct, oldVal, &oldStr, &oldLen);

        const char* newStr;
        size_t newLen;
        reduct_handle_atom_string(reduct, newVal, &newStr, &newLen);

        if (oldLen == 0)
        {
            return *handle;
        }

        const char* str = item->atom.string;

        size_t matchCount = 0;
        for (size_t i = 0; i <= item->length - oldLen;)
        {
            if (memcmp(str + i, oldStr, oldLen) == 0)
            {
                matchCount++;
                i += oldLen;
            }
            else
            {
                i++;
            }
        }

        size_t resultLen = item->length - matchCount * oldLen + matchCount * newLen;
        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        size_t lastPos = 0;
        for (size_t i = 0; i <= item->length - oldLen;)
        {
            if (memcmp(str + i, oldStr, oldLen) == 0)
            {
                if (i > lastPos)
                {
                    memcpy(dst, str + lastPos, i - lastPos);
                    dst += i - lastPos;
                }
                memcpy(dst, newStr, newLen);
                dst += newLen;
                i += oldLen;
                lastPos = i;
            }
            else
            {
                i++;
            }
        }

        if (lastPos < item->length)
        {
            memcpy(dst, str + lastPos, item->length - lastPos);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "replace: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_unique(struct reduct* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return *handle;
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(newList);

    reduct_handle_t current;
    REDUCT_LIST_FOR_EACH(&current, &item->list)
    {
        bool found = false;
        reduct_handle_t existing;
        REDUCT_LIST_FOR_EACH(&existing, newList)
        {
            if (reduct_handle_compare(reduct, &current, &existing) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            reduct_list_push(reduct, newList, current);
        }
    }

    return resultHandle;
}

REDUCT_API reduct_handle_t reduct_chunk(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* sizeH)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return *handle;
    }

    reduct_handle_t nHandle = reduct_get_int(reduct, sizeH);
    int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, n >= 0, "chunk: size must be non-negative, got %lld", n);

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    size_t chunkSize = (size_t)n;
    for (size_t i = 0; i < item->length; i += chunkSize)
    {
        size_t end = REDUCT_MIN(i + chunkSize, item->length);
        reduct_list_t* chunk = reduct_list_slice(reduct, &item->list, i, end);
        reduct_list_push(reduct, resultList, REDUCT_HANDLE_FROM_LIST(chunk));
    }

    return resultHandle;
}

REDUCT_API reduct_handle_t reduct_find(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* callable)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_handle_t current;
    REDUCT_LIST_FOR_EACH(&current, &item->list)
    {
        reduct_handle_t result = reduct_eval_call(reduct, *callable, 1, &current);
        if (REDUCT_HANDLE_IS_TRUTHY(&result))
        {
            return current;
        }
    }

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_get_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path,
    reduct_handle_t* defaultVal)
{
    assert(reduct != NULL);

    reduct_handle_t current = *list;
    reduct_handle_t pathH = *path;
    if (REDUCT_LIKELY(!REDUCT_HANDLE_IS_LIST(&pathH)))
    {
        reduct_item_t* item = reduct_handle_as_item(reduct, &current);
        if (REDUCT_UNLIKELY(item->type != REDUCT_ITEM_TYPE_LIST))
        {
            goto not_found;
        }

        reduct_handle_t entryH = reduct_list_find_entry(reduct, &item->list, &pathH);
        if (REDUCT_UNLIKELY(REDUCT_HANDLE_IS_NIL(&entryH)))
        {
            goto not_found;
        }

        reduct_item_t* entry = REDUCT_HANDLE_TO_ITEM(&entryH);
        if (REDUCT_UNLIKELY(entry->length < 2))
        {
            goto not_found;
        }

        return reduct_list_second(reduct, &entry->list);
    }

    reduct_item_t* pathItem = REDUCT_HANDLE_TO_ITEM(&pathH);
    reduct_handle_t key;
    REDUCT_LIST_FOR_EACH(&key, &pathItem->list)
    {
        reduct_item_t* item = reduct_handle_as_item(reduct, &current);
        if (REDUCT_UNLIKELY(item->type != REDUCT_ITEM_TYPE_LIST))
        {
            goto not_found;
        }

        reduct_handle_t entryH = reduct_list_find_entry(reduct, &item->list, &key);
        if (REDUCT_UNLIKELY(REDUCT_HANDLE_IS_NIL(&entryH)))
        {
            goto not_found;
        }

        reduct_item_t* entry = REDUCT_HANDLE_TO_ITEM(&entryH);
        if (REDUCT_UNLIKELY(entry->length < 2))
        {
            goto not_found;
        }

        current = reduct_list_second(reduct, &entry->list);
    }

    return current;

not_found:
    return (defaultVal != NULL) ? *defaultVal : REDUCT_HANDLE_NIL(reduct);
}

static reduct_handle_t reduct_assoc_key(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* key,
    reduct_handle_t* val)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_LIST, "assoc: expected list, got %s",
        reduct_item_type_str(item));

    size_t index;
    if (!reduct_list_find_entry_index(reduct, &item->list, key, &index))
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_list_push_list(reduct, newList, &item->list);
        reduct_list_t* newEntry = reduct_list_new(reduct);
        reduct_list_push(reduct, newEntry, *key);
        reduct_list_push(reduct, newEntry, *val);
        reduct_list_push(reduct, newList, REDUCT_HANDLE_FROM_LIST(newEntry));
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    reduct_list_t* newEntry = reduct_list_new(reduct);
    reduct_list_push(reduct, newEntry, *key);
    reduct_list_push(reduct, newEntry, *val);
    reduct_list_t* newList = reduct_list_assoc(reduct, &item->list, index, REDUCT_HANDLE_FROM_LIST(newEntry));
    return REDUCT_HANDLE_FROM_LIST(newList);
}

static reduct_handle_t reduct_dissoc_key(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* key)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    if (item->type != REDUCT_ITEM_TYPE_LIST)
    {
        return *handle;
    }

    size_t index;
    if (!reduct_list_find_entry_index(reduct, &item->list, key, &index))
    {
        return *handle;
    }

    reduct_list_t* newList = reduct_list_dissoc(reduct, &item->list, index);
    return REDUCT_HANDLE_FROM_LIST(newList);
}

REDUCT_API reduct_handle_t reduct_assoc_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path,
    reduct_handle_t* val)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(path))
    {
        return reduct_assoc_key(reduct, list, path, val);
    }

    reduct_item_t* pathItem = REDUCT_HANDLE_TO_ITEM(path);
    if (pathItem->length == 0)
    {
        return *val;
    }

    reduct_handle_t first = reduct_list_first(reduct, &pathItem->list);
    if (pathItem->length == 1)
    {
        return reduct_assoc_key(reduct, list, &first, val);
    }

    reduct_handle_t subItem = reduct_get_in(reduct, list, &first, NULL);
    reduct_handle_t restPath = REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &pathItem->list, 1, pathItem->length));
    reduct_handle_t updated = reduct_assoc_in(reduct, &subItem, &restPath, val);

    return reduct_assoc_key(reduct, list, &first, &updated);
}

REDUCT_API reduct_handle_t reduct_dissoc_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(path))
    {
        return reduct_dissoc_key(reduct, list, path);
    }

    reduct_item_t* pathItem = REDUCT_HANDLE_TO_ITEM(path);
    if (pathItem->length == 0)
    {
        return *list;
    }

    reduct_handle_t first = reduct_list_first(reduct, &pathItem->list);
    if (pathItem->length == 1)
    {
        return reduct_dissoc_key(reduct, list, &first);
    }

    reduct_handle_t subItem = reduct_get_in(reduct, list, &first, NULL);
    if (REDUCT_HANDLE_IS_NIL(&subItem))
    {
        return *list;
    }

    reduct_handle_t restPath = REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &pathItem->list, 1, pathItem->length));
    reduct_handle_t updated = reduct_dissoc_in(reduct, &subItem, &restPath);

    return reduct_assoc_key(reduct, list, &first, &updated);
}

REDUCT_API reduct_handle_t reduct_update_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path,
    reduct_handle_t* callable)
{
    assert(reduct != NULL);

    reduct_handle_t currentVal = reduct_get_in(reduct, list, path, NULL);
    reduct_handle_t newVal = reduct_eval_call(reduct, *callable, 1, &currentVal);

    return reduct_assoc_in(reduct, list, path, &newVal);
}

static inline reduct_handle_t reduct_list_project(reduct_t* reduct, reduct_handle_t* listHandle, size_t index,
    const char* name)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "%s: expected list, got %s", name,
        REDUCT_HANDLE_GET_TYPE_STR(listHandle));
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(listHandle);
    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    reduct_handle_t childHandle;
    REDUCT_LIST_FOR_EACH(&childHandle, &item->list)
    {
        if (REDUCT_HANDLE_IS_LIST(&childHandle))
        {
            reduct_item_t* childItem = REDUCT_HANDLE_TO_ITEM(&childHandle);
            if (childItem->length > index)
            {
                reduct_list_push(reduct, resultList, reduct_list_nth(reduct, &childItem->list, index));
            }
        }
    }

    return resultHandle;
}

REDUCT_API reduct_handle_t reduct_keys(reduct_t* reduct, reduct_handle_t* listHandle)
{
    assert(reduct != NULL);
    return reduct_list_project(reduct, listHandle, 0, "keys");
}

REDUCT_API reduct_handle_t reduct_values(reduct_t* reduct, reduct_handle_t* listHandle)
{
    assert(reduct != NULL);
    return reduct_list_project(reduct, listHandle, 1, "values");
}

REDUCT_API reduct_handle_t reduct_merge(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t result = REDUCT_HANDLE_FROM_LIST(resultList);

    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            continue;
        }

        reduct_item_t* currentItem = REDUCT_HANDLE_TO_ITEM(&argv[i]);
        reduct_handle_t entryH;
        REDUCT_LIST_FOR_EACH(&entryH, &currentItem->list)
        {
            reduct_handle_t key, val;
            if (reduct_list_get_entry(reduct, &entryH, &key, &val))
            {
                reduct_handle_t next = reduct_assoc_in(reduct, &result, &key, &val);
                result = next;
            }
        }
    }

    return result;
}

REDUCT_API reduct_handle_t reduct_explode(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    reduct_list_t* list = reduct_list_new(reduct);
    reduct_handle_t listHandle = REDUCT_HANDLE_FROM_LIST(list);

    for (size_t i = 0; i < argc; i++)
    {
        const char* str;
        size_t len;
        reduct_handle_atom_string(reduct, &argv[i], &str, &len);
        for (size_t j = 0; j < len; j++)
        {
            reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_INT((int64_t)(unsigned char)str[j]));
        }
    }

    return listHandle;
}

REDUCT_API reduct_handle_t reduct_implode(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    size_t totalLen = 0;
    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            continue;
        }
        totalLen += REDUCT_HANDLE_TO_ITEM(&argv[i])->length;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dst = result->string;

    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            continue;
        }
        reduct_item_t* list = REDUCT_HANDLE_TO_ITEM(&argv[i]);
        reduct_handle_t valH;
        REDUCT_LIST_FOR_EACH(&valH, &list->list)
        {
            reduct_handle_t charH = reduct_get_int(reduct, &valH);
            *dst++ = (char)REDUCT_HANDLE_TO_INT(&charH);
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_repeat(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* count)
{
    assert(reduct != NULL);

    reduct_handle_t nHandle = reduct_get_int(reduct, count);
    int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, n >= 0, "repeat: count must be non-negative, got %lld", n);

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (size_t i = 0; i < (size_t)n; i++)
    {
        reduct_list_push(reduct, newList, *handle);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_check_edge(reduct_t* reduct, reduct_handle_t* handle,
    reduct_handle_t* target, bool start, const char* name)
{
    assert(reduct != NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "%s: expected list or atom, got %s", name, REDUCT_HANDLE_GET_TYPE_STR(handle));
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);

    if (item->type == REDUCT_ITEM_TYPE_LIST)
    {
        if (item->length == 0)
        {
            return REDUCT_HANDLE_FALSE();
        }
        size_t index = start ? 0 : item->length - 1;
        reduct_handle_t edge = reduct_list_nth(reduct, &item->list, index);
        return (reduct_handle_compare(reduct, &edge, target) == 0) ? REDUCT_HANDLE_TRUE() : REDUCT_HANDLE_FALSE();
    }
    else
    {
        const char *srcStr, *tgtStr;
        size_t srcLen, tgtLen;
        reduct_handle_atom_string(reduct, handle, &srcStr, &srcLen);
        reduct_handle_atom_string(reduct, target, &tgtStr, &tgtLen);

        if (tgtLen > srcLen)
        {
            return REDUCT_HANDLE_FALSE();
        }
        const char* offset = start ? srcStr : srcStr + srcLen - tgtLen;
        return (memcmp(offset, tgtStr, tgtLen) == 0) ? REDUCT_HANDLE_TRUE() : REDUCT_HANDLE_FALSE();
    }
    return REDUCT_HANDLE_FALSE();
}

REDUCT_API reduct_handle_t reduct_starts_with(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* prefix)
{
    return reduct_sequence_check_edge(reduct, handle, prefix, true, "starts-with?");
}

REDUCT_API reduct_handle_t reduct_ends_with(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* suffix)
{
    return reduct_sequence_check_edge(reduct, handle, suffix, false, "ends-with?");
}

REDUCT_API reduct_handle_t reduct_join(reduct_t* reduct, reduct_handle_t* listHandle, reduct_handle_t* sepHandle)
{
    assert(reduct != NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle),
        "join: first argument must be a list, got %s", REDUCT_HANDLE_GET_TYPE_STR(listHandle));

    reduct_item_t* list = REDUCT_HANDLE_TO_ITEM(listHandle);
    if (list->length == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* sepAtom = &reduct_handle_as_item(reduct, sepHandle)->atom;

    size_t totalLen = 0;
    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &list->list)
    {
        totalLen += reduct_handle_as_item(reduct, &entry)->atom.length;
        if (_iter.index + 1 < list->length)
        {
            totalLen += sepAtom->length;
        }
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dst = result->string;

    REDUCT_LIST_FOR_EACH(&entry, &list->list)
    {
        reduct_atom_t* src = &reduct_handle_as_item(reduct, &entry)->atom;
        memcpy(dst, src->string, src->length);
        dst += src->length;
        if (_iter.index + 1 < list->length)
        {
            memcpy(dst, sepAtom->string, sepAtom->length);
            dst += sepAtom->length;
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_split(reduct_t* reduct, reduct_handle_t* srcHandle, reduct_handle_t* sepHandle)
{
    assert(reduct != NULL);

    const char *srcStr, *sepStr;
    size_t srcLen, sepLen;

    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);
    reduct_handle_atom_string(reduct, sepHandle, &sepStr, &sepLen);

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    if (sepLen == 0)
    {
        for (size_t i = 0; i < srcLen; i++)
        {
            reduct_list_push(reduct, resultList,
                REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), i, 1)));
        }
    }
    else
    {
        size_t lastPos = 0;
        for (size_t i = 0; i <= srcLen - sepLen; i++)
        {
            if (memcmp(srcStr + i, sepStr, sepLen) == 0)
            {
                reduct_list_push(reduct, resultList,
                    REDUCT_HANDLE_FROM_ATOM(
                        reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, i - lastPos)));
                i += sepLen - 1;
                lastPos = i + 1;
            }
        }

        reduct_list_push(reduct, resultList,
            REDUCT_HANDLE_FROM_ATOM(
                reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, srcLen - lastPos)));
    }

    return resultHandle;
}

static inline reduct_handle_t reduct_string_transform(reduct_t* reduct, reduct_handle_t* srcHandle, bool upper)
{
    const char* srcStr;
    size_t srcLen;
    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return *srcHandle;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, srcLen);
    char* dst = result->string;
    for (size_t i = 0; i < srcLen; i++)
    {
        dst[i] = upper ? REDUCT_CHAR_TO_UPPER(srcStr[i]) : REDUCT_CHAR_TO_LOWER(srcStr[i]);
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_upper(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    assert(reduct != NULL);
    return reduct_string_transform(reduct, srcHandle, true);
}

REDUCT_API reduct_handle_t reduct_lower(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    assert(reduct != NULL);
    return reduct_string_transform(reduct, srcHandle, false);
}

REDUCT_API reduct_handle_t reduct_trim(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    assert(reduct != NULL);

    const char* srcStr;
    size_t srcLen;
    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return *srcHandle;
    }

    size_t start = 0;
    while (start < srcLen && REDUCT_CHAR_IS_WHITESPACE(srcStr[start]))
    {
        start++;
    }

    if (start == srcLen)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    size_t end = srcLen - 1;
    while (end > start && REDUCT_CHAR_IS_WHITESPACE(srcStr[end]))
    {
        end--;
    }

    return REDUCT_HANDLE_FROM_ATOM(
        reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), start, end - start + 1));
}

#define REDUCT_INTROSPECTION_LOOP(_predicate) \
    do \
    { \
        assert(reduct != NULL); \
        assert(argv != NULL || argc == 0); \
        for (size_t i = 0; i < argc; i++) \
        { \
            if (!(_predicate)) \
            { \
                return REDUCT_HANDLE_FALSE(); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    } while (0)

#define REDUCT_INTROSPECTION_IMPL(_name, _predicate_macro) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_UNUSED(reduct); \
        REDUCT_INTROSPECTION_LOOP(_predicate_macro(&argv[i])); \
    }

REDUCT_INTROSPECTION_IMPL(reduct_is_atom, REDUCT_HANDLE_IS_ATOM_LIKE)
REDUCT_INTROSPECTION_IMPL(reduct_is_int, REDUCT_HANDLE_IS_INT_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_float, REDUCT_HANDLE_IS_FLOAT_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_number, REDUCT_HANDLE_IS_NUMBER_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_lambda, REDUCT_HANDLE_IS_LAMBDA)
REDUCT_INTROSPECTION_IMPL(reduct_is_list, REDUCT_HANDLE_IS_LIST)

REDUCT_API reduct_handle_t reduct_is_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_NATIVE(reduct, &argv[i]));
}

REDUCT_API reduct_handle_t reduct_is_callable(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_CALLABLE(reduct, &argv[i]));
}

#define REDUCT_PREDICATE_IS_EMPTY(_h) (reduct_handle_as_item(reduct, _h)->length == 0)
REDUCT_INTROSPECTION_IMPL(reduct_is_empty, REDUCT_PREDICATE_IS_EMPTY)

REDUCT_INTROSPECTION_IMPL(reduct_is_nil, REDUCT_HANDLE_IS_NIL)

REDUCT_API reduct_handle_t reduct_get_int(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    if (REDUCT_HANDLE_IS_INT(handle))
    {
        return *handle;
    }
    if (REDUCT_HANDLE_IS_FLOAT(handle))
    {
        return REDUCT_HANDLE_FROM_INT((int64_t)REDUCT_HANDLE_TO_FLOAT(handle));
    }

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_ATOM, "expected atom, got %s",
        reduct_item_type_str(item));

    if (reduct_atom_is_number(&item->atom))
    {
        return REDUCT_HANDLE_FROM_INT(reduct_atom_get_int(&item->atom));
    }

    const char* str = item->atom.string;
    size_t len = item->atom.length;
    reduct_atom_t* atom = reduct_atom_lookup(reduct, str, len, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(atom))
    {
        return REDUCT_HANDLE_FROM_INT(reduct_atom_get_int(atom));
    }

    return REDUCT_HANDLE_FROM_INT(0);
}

REDUCT_API reduct_handle_t reduct_get_float(reduct_t* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    if (REDUCT_HANDLE_IS_FLOAT(handle))
    {
        return *handle;
    }
    if (REDUCT_HANDLE_IS_INT(handle))
    {
        return REDUCT_HANDLE_FROM_FLOAT((double)REDUCT_HANDLE_TO_INT(handle));
    }

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_ATOM, "expected atom, got %s",
        reduct_item_type_str(item));

    if (reduct_atom_is_number(&item->atom))
    {
        return REDUCT_HANDLE_FROM_FLOAT(reduct_atom_get_float(&item->atom));
    }

    const char* str = item->atom.string;
    size_t len = item->atom.length;
    reduct_atom_t* atom = reduct_atom_lookup(reduct, str, len, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(atom))
    {
        return REDUCT_HANDLE_FROM_FLOAT(reduct_atom_get_float(atom));
    }

    return REDUCT_HANDLE_FROM_FLOAT(0.0);
}

static void reduct_path_copy(reduct_t* reduct, char* dest, const char* src, size_t len, size_t max)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, len < max, "path exceeds maximum length");
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static void reduct_path_normalize(char* path, size_t* outLen)
{
    size_t len = *outLen;
    size_t w = 0;
    bool isAbsolute = false;

    if (len > 0 && (path[0] == '/' || path[0] == '\\'))
    {
        isAbsolute = true;
        w = 1;
    }

    for (size_t r = (isAbsolute ? 1 : 0); r < len;)
    {
        while (r < len && (path[r] == '/' || path[r] == '\\'))
        {
            r++;
        }
        if (r >= len)
            break;

        size_t compStart = r;
        while (r < len && path[r] != '/' && path[r] != '\\')
        {
            r++;
        }
        size_t compLen = r - compStart;

        if (compLen == 1 && path[compStart] == '.')
        {
            continue;
        }

        if (compLen == 2 && path[compStart] == '.' && path[compStart + 1] == '.')
        {
            if (w > (isAbsolute ? 1 : 0))
            {
                size_t lastCompStart = w;
                while (lastCompStart > 0 && path[lastCompStart - 1] != '/')
                {
                    lastCompStart--;
                }
                if (isAbsolute && lastCompStart == 0)
                {
                    lastCompStart = 1;
                }

                size_t lastLen = w - lastCompStart;
                if (lastLen == 2 && path[lastCompStart] == '.' && path[lastCompStart + 1] == '.')
                {
                    path[w++] = '/';
                    path[w++] = '.';
                    path[w++] = '.';
                }
                else
                {
                    w = lastCompStart;
                    if (w > 0 && path[w - 1] == '/')
                    {
                        w--;
                    }
                    if (w == 0 && isAbsolute)
                    {
                        w = 1;
                    }
                }
            }
            else if (!isAbsolute)
            {
                if (w > 0)
                {
                    path[w++] = '/';
                }
                path[w++] = '.';
                path[w++] = '.';
            }
            continue;
        }

        if (w > 0 && path[w - 1] != '/')
        {
            path[w++] = '/';
        }
        for (size_t i = 0; i < compLen; i++)
        {
            char c = path[compStart + i];
            path[w++] = (c == '\\') ? '/' : c;
        }
    }

    if (w == 0)
    {
        path[w++] = '.';
    }

    path[w] = '\0';
    *outLen = w;
}

static void reduct_resolve_path(reduct_t* reduct, const char* path, size_t pathLen, char* outPath, size_t maxLen,
    bool checkExistence)
{
    char normalized[REDUCT_PATH_MAX];
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, pathLen < REDUCT_PATH_MAX, "path exceeds maximum length");
    memcpy(normalized, path, pathLen);
    normalized[pathLen] = '\0';
    reduct_path_normalize(normalized, &pathLen);

    if (pathLen > 0 && (normalized[0] == '/'))
    {
        reduct_path_copy(reduct, outPath, normalized, pathLen, maxLen);
        return;
    }
    if (pathLen > 2 &&
        ((normalized[0] >= 'a' && normalized[0] <= 'z') || (normalized[0] >= 'A' && normalized[0] <= 'Z')) &&
        normalized[1] == ':')
    {
        reduct_path_copy(reduct, outPath, normalized, pathLen, maxLen);
        return;
    }

    if (reduct == NULL || reduct->frameCount == 0)
    {
        goto fallback;
    }

    reduct_input_t* input = NULL;
    for (size_t i = reduct->frameCount; i > 0; i--)
    {
        reduct_eval_frame_t* frame = &reduct->frames[i - 1];
        reduct_item_t* funcItem = REDUCT_CONTAINER_OF(frame->closure->function, reduct_item_t, function);
        reduct_input_t* funcInput = reduct_input_lookup(reduct, funcItem->inputId);
        if (funcInput != NULL && funcInput->path[0] != '\0')
        {
            input = funcInput;
            break;
        }
    }

    if (input != NULL)
    {
        const char* lastSlash = NULL;
        const char* p = input->path;
        while (*p != '\0')
        {
            if (*p == '/' || *p == '\\')
            {
                lastSlash = p;
            }
            p++;
        }

        if (lastSlash != NULL)
        {
            size_t dirLen = (size_t)(lastSlash - input->path) + 1;
            if (dirLen + pathLen < maxLen)
            {
                memcpy(outPath, input->path, dirLen);
                memcpy(outPath + dirLen, normalized, pathLen);
                outPath[dirLen + pathLen] = '\0';

                if (!checkExistence)
                {
                    return;
                }

                struct stat st;
                if (stat(outPath, &st) == 0)
                {
                    return;
                }
            }
        }
    }

    if (reduct != NULL && reduct->importPaths != NULL)
    {
        for (size_t i = 0; i < reduct->importPathCount; i++)
        {
            const char* includeDir = reduct->importPaths[i];
            size_t dirLen = strlen(includeDir);
            size_t needSep = (dirLen > 0 && includeDir[dirLen - 1] != '/') ? 1 : 0;
            size_t totalLen = dirLen + needSep + pathLen;
            if (totalLen + 1 < maxLen)
            {
                memcpy(outPath, includeDir, dirLen);
                if (needSep)
                {
                    outPath[dirLen] = '/';
                }
                memcpy(outPath + dirLen + needSep, normalized, pathLen);
                outPath[totalLen] = '\0';

                if (!checkExistence)
                {
                    return;
                }

                struct stat st;
                if (stat(outPath, &st) == 0)
                {
                    return;
                }
            }
        }
    }

fallback:
    reduct_path_copy(reduct, outPath, normalized, pathLen, maxLen);
}

REDUCT_API reduct_handle_t reduct_run(struct reduct* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, handle, &str, &len);

    reduct_handle_t ast = reduct_parse(reduct, str, len, "<run>");
    reduct_function_t* function = reduct_compile(reduct, &ast);
    return reduct_eval(reduct, function);
}

static void reduct_get_resolved_path(reduct_t* reduct, reduct_handle_t* pathHandle, char* outBuf)
{
    const char* pathStr;
    size_t pathLen;
    reduct_handle_atom_string(reduct, pathHandle, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, outBuf, REDUCT_PATH_MAX, true);
}

REDUCT_API reduct_handle_t reduct_import(struct reduct* reduct, reduct_handle_t* path, reduct_handle_t* compiler,
    reduct_handle_t* compilerArgs)
{
    assert(reduct != NULL);
    char libPathBuf[REDUCT_PATH_MAX];
    char pathBuf[REDUCT_PATH_MAX];
    reduct_lib_t lib;

    reduct_get_resolved_path(reduct, path, pathBuf);
    char* pathString = pathBuf;

    const char* ext = strrchr(pathBuf, '.');
    if (ext == NULL)
    {
        ext = "";
    }
    else
    {
        ext++;
    }

    if (strcmp(ext, "c") == 0)
    {
        strncpy(libPathBuf, pathBuf, REDUCT_PATH_MAX);

        char* lastDot = strrchr(libPathBuf, '.');
        if (lastDot != NULL)
        {
            *lastDot = '\0';
        }
        strncat(libPathBuf, ".rdt.so", REDUCT_PATH_MAX - 1);

        struct stat statLib;
        if (stat(libPathBuf, &statLib) == 0)
        {
            struct stat statSrc;
            if (stat(pathBuf, &statSrc) == 0)
            {
                if (statLib.st_mtime >= statSrc.st_mtime)
                {
                    pathString = libPathBuf;
                    ext = "so";
                    goto load_shared_lib;
                }
            }
        }

        REDUCT_ERROR_RUNTIME_ASSERT(reduct, compiler != NULL, "import: C source import requires a compiler");

        const char* compilerStr;
        size_t compilerLen;
        reduct_handle_atom_string(reduct, compiler, &compilerStr, &compilerLen);

        const char* compilerArgsStr;
        size_t compilerArgsLen;
        if (compilerArgs != NULL)
        {
            reduct_handle_atom_string(reduct, compilerArgs, &compilerArgsStr, &compilerArgsLen);
        }

        size_t bufferCapacity =
            compilerLen + (compilerArgs != NULL ? compilerArgsLen : 0) + strlen(pathBuf) + strlen(libPathBuf) + 64;
        REDUCT_SCRATCH(reduct, buffer, char, bufferCapacity);
        snprintf(buffer, bufferCapacity, "%.*s %.*s %s -shared -fPIC -o %s", (int)compilerLen, compilerStr,
            (compilerArgs != NULL ? (int)compilerArgsLen : 0), (compilerArgs != NULL ? compilerArgsStr : ""), pathBuf,
            libPathBuf);

        int status = system(buffer);
        REDUCT_SCRATCH_FREE(reduct, buffer);
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, status == 0, "import: compilation failed with status %d", status);

        pathString = libPathBuf;
        ext = "so";
        goto load_shared_lib;
    }

    if (strcmp(ext, "so") == 0 || strcmp(ext, "dll") == 0 || strcmp(ext, "dylib") == 0)
    {
load_shared_lib:
        lib = REDUCT_LIB_OPEN(pathString);
        if (lib == NULL)
        {
            REDUCT_ERROR_RUNTIME(reduct, REDUCT_LIB_ERROR());
        }

        reduct_module_init_fn init = (reduct_module_init_fn)REDUCT_LIB_SYM(lib, REDUCT_LIB_ENTRY);
        if (init == NULL)
        {
            REDUCT_LIB_CLOSE(lib);
            REDUCT_ERROR_RUNTIME(reduct, "could not find %s in %s", REDUCT_LIB_ENTRY, pathString);
        }

        if (reduct->libs == NULL)
        {
            reduct->libCapacity = REDUCT_LIBS_INITIAL;
            reduct->libs = (reduct_lib_t*)calloc(reduct->libCapacity, sizeof(reduct_lib_t));
            if (reduct->libs == NULL)
            {
                REDUCT_ERROR_INTERNAL(reduct, "out of memory");
            }
        }
        else if (reduct->libCount >= reduct->libCapacity)
        {
            reduct->libCapacity *= 2;
            reduct_lib_t* newLibs = (reduct_lib_t*)realloc(reduct->libs, reduct->libCapacity * sizeof(reduct_lib_t));
            if (newLibs == NULL)
            {
                REDUCT_ERROR_INTERNAL(reduct, "out of memory");
            }
            reduct->libs = newLibs;
        }
        reduct->libs[reduct->libCount++] = lib;

        return init(reduct);
    }

    reduct_handle_t ast = reduct_parse_file(reduct, pathString);
    reduct_function_t* function = reduct_compile(reduct, &ast);
    return reduct_eval(reduct, function);
}

REDUCT_API reduct_handle_t reduct_read_file(struct reduct* reduct, reduct_handle_t* path)
{
    assert(reduct != NULL);
    char pathBuf[REDUCT_PATH_MAX];
    reduct_get_resolved_path(reduct, path, pathBuf);

    FILE* file = fopen(pathBuf, "rb");
    if (file == NULL)
    {
        REDUCT_ERROR_RUNTIME(reduct, "could not open %s", pathBuf);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    reduct_atom_t* atom = reduct_atom_new(reduct, (size_t)size);
    if (atom->string == NULL)
    {
        fclose(file);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    if (fread(atom->string, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        REDUCT_ERROR_RUNTIME(reduct, "could not read %s", pathBuf);
    }

    fclose(file);
    return REDUCT_HANDLE_FROM_ATOM(atom);
}

REDUCT_API reduct_handle_t reduct_write_file(struct reduct* reduct, reduct_handle_t* path, reduct_handle_t* content)
{
    assert(reduct != NULL);

    char pathBuf[REDUCT_PATH_MAX];
    const char* pathStr;
    size_t pathLen;
    reduct_handle_atom_string(reduct, path, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, pathBuf, REDUCT_PATH_MAX, false);

    const char* contentStr;
    size_t contentLen;
    reduct_handle_atom_string(reduct, content, &contentStr, &contentLen);

    FILE* file = fopen(pathBuf, "wb");
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, file != NULL, "could not open %s for writing", pathBuf);

    if (fwrite(contentStr, 1, contentLen, file) != contentLen)
    {
        fclose(file);
        REDUCT_ERROR_RUNTIME(reduct, "could not write to %s", pathBuf);
    }

    fclose(file);
    return *content;
}

REDUCT_API reduct_handle_t reduct_read_char(struct reduct* reduct)
{
    assert(reduct != NULL);

    int c = fgetc(stdin);
    if (c == EOF)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    char ch = (char)c;
    return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &ch, 1));
}

REDUCT_API reduct_handle_t reduct_read_line(struct reduct* reduct)
{
    assert(reduct != NULL);

    REDUCT_SCRATCH(reduct, buffer, char, REDUCT_SCRATCH_INITIAL);
    size_t length = 0;

    while (true)
    {
        int c = fgetc(stdin);
        if (c == EOF || c == '\n')
        {
            if (c == EOF && length == 0)
            {
                REDUCT_SCRATCH_FREE(reduct, buffer);
                return REDUCT_HANDLE_NIL(reduct);
            }
            break;
        }

        REDUCT_SCRATCH_GROW(reduct, buffer, char, length + 1);
        buffer[length++] = (char)c;
    }

    reduct_handle_t result = REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, buffer, length));

    REDUCT_SCRATCH_FREE(reduct, buffer);

    return result;
}

REDUCT_API reduct_handle_t reduct_print(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);

    for (size_t i = 0; i < argc; i++)
    {
        if (REDUCT_HANDLE_IS_INT(&argv[i]))
        {
            int64_t val = REDUCT_HANDLE_TO_INT(&argv[i]);
            fprintf(stdout, "%lld", (long long)val);
        }
        else if (REDUCT_HANDLE_IS_FLOAT(&argv[i]))
        {
            double val = REDUCT_HANDLE_TO_FLOAT(&argv[i]);
            fprintf(stdout, "%f", val);
        }
        else if (REDUCT_HANDLE_IS_ATOM(&argv[i]) && REDUCT_HANDLE_TO_ATOM(&argv[i])->flags & REDUCT_ATOM_FLAG_QUOTED)
        {
            const char* str;
            size_t len;
            reduct_handle_atom_string(reduct, &argv[i], &str, &len);
            fwrite(str, 1, len, stdout);
        }
        else
        {
            size_t len = reduct_stringify(reduct, &argv[i], NULL, 0);
            REDUCT_SCRATCH(reduct, buffer, char, len + 1);
            reduct_stringify(reduct, &argv[i], buffer, len + 1);
            fwrite(buffer, 1, len, stdout);
            REDUCT_SCRATCH_FREE(reduct, buffer);
        }

        if (i < argc - 1)
        {
            fwrite(" ", 1, 1, stdout);
        }
    }
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_println(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);

    reduct_handle_t handle = reduct_print(reduct, argc, argv);
    fwrite("\n", 1, 1, stdout);
    return handle;
}

REDUCT_API reduct_handle_t reduct_ord(struct reduct* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);
    assert(handle != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, handle, &str, &len);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, len > 0, "ord: expected a non-empty atom");

    return REDUCT_HANDLE_FROM_INT((int64_t)(uint8_t)str[0]);
}

REDUCT_API reduct_handle_t reduct_chr(struct reduct* reduct, reduct_handle_t* handle)
{
    assert(reduct != NULL);

    reduct_handle_t iVal = reduct_get_int(reduct, handle);
    int64_t val = REDUCT_HANDLE_TO_INT(&iVal);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, val >= 0 && val <= 255, "chr: expected integer 0-255, got %lld",
        (long long)val);

    char c = (char)(uint8_t)val;
    return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &c, 1));
}

REDUCT_API reduct_handle_t reduct_format(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);

    if (argc == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    const char* fmtStr;
    size_t fmtLen;
    reduct_handle_atom_string(reduct, &argv[0], &fmtStr, &fmtLen);

    size_t totalLen = 0;
    size_t argIndex = 1;

    for (size_t i = 0; i < fmtLen; i++)
    {
        if (fmtStr[i] == '{')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '{')
            {
                totalLen++;
                i++;
                continue;
            }

            size_t j = i + 1;
            int64_t explicitIndex = -1;
            if (j < fmtLen && fmtStr[j] >= '0' && fmtStr[j] <= '9')
            {
                explicitIndex = 0;
                while (j < fmtLen && fmtStr[j] >= '0' && fmtStr[j] <= '9')
                {
                    explicitIndex = explicitIndex * 10 + (fmtStr[j] - '0');
                    j++;
                }
            }

            if (j < fmtLen && fmtStr[j] == '}')
            {
                size_t idx = (explicitIndex != -1) ? (size_t)explicitIndex + 1 : argIndex++;
                if (idx >= argc)
                {
                    REDUCT_ERROR_RUNTIME(reduct, "format: argument index out of range");
                }
                if (REDUCT_HANDLE_IS_ATOM_LIKE(&argv[idx]))
                {
                    const char* str;
                    size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    totalLen += len;
                }
                else
                {
                    totalLen += reduct_stringify(reduct, &argv[idx], NULL, 0);
                }
                i = j;
                continue;
            }

            REDUCT_ERROR_RUNTIME(reduct, "format: invalid format specifier");
        }
        else if (fmtStr[i] == '}')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '}')
            {
                totalLen++;
                i++;
                continue;
            }

            REDUCT_ERROR_RUNTIME(reduct, "format: unmatched '}'");
        }
        totalLen++;
    }

    reduct_atom_t* resultAtom = reduct_atom_new(reduct, totalLen);
    char* buffer = resultAtom->string;

    size_t currentPos = 0;
    argIndex = 1;

    for (size_t i = 0; i < fmtLen; i++)
    {
        if (fmtStr[i] == '{')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '{')
            {
                buffer[currentPos++] = '{';
                i++;
                continue;
            }

            size_t j = i + 1;
            int64_t explicitIndex = -1;
            if (j < fmtLen && fmtStr[j] >= '0' && fmtStr[j] <= '9')
            {
                explicitIndex = 0;
                while (j < fmtLen && fmtStr[j] >= '0' && fmtStr[j] <= '9')
                {
                    explicitIndex = explicitIndex * 10 + (fmtStr[j] - '0');
                    j++;
                }
            }

            if (j < fmtLen && fmtStr[j] == '}')
            {
                size_t idx = (explicitIndex != -1) ? (size_t)explicitIndex + 1 : argIndex++;
                if (REDUCT_HANDLE_IS_ATOM_LIKE(&argv[idx]))
                {
                    const char* str;
                    size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    memcpy(buffer + currentPos, str, len);
                    currentPos += len;
                }
                else
                {
                    currentPos += reduct_stringify(reduct, &argv[idx], buffer + currentPos, totalLen - currentPos + 1);
                }
                i = j;
                continue;
            }
        }
        else if (fmtStr[i] == '}')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '}')
            {
                buffer[currentPos++] = '}';
                i++;
                continue;
            }
        }
        buffer[currentPos++] = fmtStr[i];
    }

    return REDUCT_HANDLE_FROM_ATOM(resultAtom);
}

REDUCT_API reduct_handle_t reduct_now(reduct_t* reduct)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);

    return REDUCT_HANDLE_FROM_FLOAT((double)time(NULL));
}

REDUCT_API reduct_handle_t reduct_uptime(reduct_t* reduct)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);

    return REDUCT_HANDLE_FROM_FLOAT((double)clock());
}

REDUCT_API reduct_handle_t reduct_env(struct reduct* reduct)
{
    assert(reduct != NULL);

    extern char** environ;
    size_t count = 0;
    while (environ[count] != NULL)
    {
        count++;
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (size_t i = 0; i < count; i++)
    {
        char* env = environ[i];
        char* eq = strchr(env, '=');
        if (eq != NULL)
        {
            reduct_list_t* pair = reduct_list_new(reduct);
            reduct_list_push(reduct, pair,
                REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, env, (size_t)(eq - env))));
            reduct_list_push(reduct, pair,
                REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, eq + 1, strlen(eq + 1))));

            reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_LIST(pair));
        }
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

REDUCT_API reduct_handle_t reduct_args(struct reduct* reduct)
{
    assert(reduct != NULL);

    if (reduct->argc == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (int i = 0; i < reduct->argc; i++)
    {
        reduct_list_push(reduct, list,
            REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, reduct->argv[i], strlen(reduct->argv[i]))));
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

#define REDUCT_MATH_MIN_MAX_IMPL(_name, _op) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        assert(reduct != NULL); \
        if (argc == 0) \
        { \
            return REDUCT_HANDLE_NIL(reduct); \
        } \
        reduct_handle_t current = argv[0]; \
        for (size_t i = 1; i < argc; i++) \
        { \
            reduct_promotion_t prom; \
            reduct_handle_promote(reduct, &current, &argv[i], &prom); \
            if (prom.type == REDUCT_PROMOTION_TYPE_INT) \
            { \
                current = REDUCT_HANDLE_FROM_INT(prom.a.intVal _op prom.b.intVal ? prom.a.intVal : prom.b.intVal); \
            } \
            else \
            { \
                current = \
                    REDUCT_HANDLE_FROM_FLOAT(prom.a.floatVal _op prom.b.floatVal ? prom.a.floatVal : prom.b.floatVal); \
            } \
        } \
        return current; \
    }

REDUCT_MATH_MIN_MAX_IMPL(reduct_min, <)
REDUCT_MATH_MIN_MAX_IMPL(reduct_max, >)

REDUCT_API reduct_handle_t reduct_clamp(reduct_t* reduct, reduct_handle_t* val, reduct_handle_t* minVal,
    reduct_handle_t* maxVal)
{
    assert(reduct != NULL);

    reduct_handle_t current = *val;
    reduct_promotion_t prom;

    reduct_handle_promote(reduct, &current, minVal, &prom);
    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        current = REDUCT_HANDLE_FROM_INT(prom.a.intVal < prom.b.intVal ? prom.b.intVal : prom.a.intVal);
    }
    else
    {
        current = REDUCT_HANDLE_FROM_FLOAT(prom.a.floatVal < prom.b.floatVal ? prom.b.floatVal : prom.a.floatVal);
    }

    reduct_handle_promote(reduct, &current, maxVal, &prom);
    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        current = REDUCT_HANDLE_FROM_INT(prom.a.intVal > prom.b.intVal ? prom.b.intVal : prom.a.intVal);
    }
    else
    {
        current = REDUCT_HANDLE_FROM_FLOAT(prom.a.floatVal > prom.b.floatVal ? prom.b.floatVal : prom.a.floatVal);
    }

    return current;
}

#define REDUCT_MATH_UNARY_IMPL(_name, _intFunc, _floatFunc) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_handle_t* val) \
    { \
        assert(reduct != NULL); \
        if (REDUCT_HANDLE_IS_INT_SHAPED(val)) \
        { \
            reduct_handle_t iVal = reduct_get_int(reduct, val); \
            int64_t i = REDUCT_HANDLE_TO_INT(&iVal); \
            return REDUCT_HANDLE_FROM_INT((int64_t)_intFunc(i)); \
        } \
        reduct_handle_t floatVal = reduct_get_float(reduct, val); \
        double f = REDUCT_HANDLE_TO_FLOAT(&floatVal); \
        return REDUCT_HANDLE_FROM_FLOAT((double)_floatFunc(f)); \
    }

#define REDUCT_INT_ABS(_x) ((_x) < 0 ? -(_x) : (_x))
REDUCT_MATH_UNARY_IMPL(reduct_abs, REDUCT_INT_ABS, fabs)
REDUCT_MATH_UNARY_IMPL(reduct_exp, exp, exp)
REDUCT_MATH_UNARY_IMPL(reduct_sqrt, sqrt, sqrt)

#define REDUCT_MATH_UNARY_TO_INT_IMPL(_name, _float_func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t* val) \
    { \
        assert(reduct != NULL); \
        if (REDUCT_HANDLE_IS_INT_SHAPED(val)) \
        { \
            return reduct_get_int(reduct, val); \
        } \
        reduct_handle_t floatVal = reduct_get_float(reduct, val); \
        double f = REDUCT_HANDLE_TO_FLOAT(&floatVal); \
        return REDUCT_HANDLE_FROM_INT((int64_t)_float_func(f)); \
    }

REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_floor, floor)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_ceil, ceil)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_round, round)

REDUCT_API reduct_handle_t reduct_pow(reduct_t* reduct, reduct_handle_t* base, reduct_handle_t* exp)
{
    assert(reduct != NULL);

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, base, exp, &prom);

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        return REDUCT_HANDLE_FROM_INT((int64_t)pow((double)prom.a.intVal, (double)prom.b.intVal));
    }
    return REDUCT_HANDLE_FROM_FLOAT((double)pow(prom.a.floatVal, prom.b.floatVal));
}

REDUCT_API reduct_handle_t reduct_log(struct reduct* reduct, reduct_handle_t* val, reduct_handle_t* base)
{
    assert(reduct != NULL);

    if (base == NULL)
    {
        if (REDUCT_HANDLE_IS_INT_SHAPED(val))
        {
            reduct_handle_t iVal = reduct_get_int(reduct, val);
            int64_t i = REDUCT_HANDLE_TO_INT(&iVal);
            return REDUCT_HANDLE_FROM_INT((int64_t)log(i));
        }

        reduct_handle_t floatVal = reduct_get_float(reduct, val);
        double f = REDUCT_HANDLE_TO_FLOAT(&floatVal);
        return REDUCT_HANDLE_FROM_FLOAT((double)log(f));
    }

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, val, base, &prom);

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        double res = log((double)prom.a.intVal) / log((double)prom.b.intVal);
        return REDUCT_HANDLE_FROM_INT((int64_t)res);
    }
    double res = log(prom.a.floatVal) / log(prom.b.floatVal);
    return REDUCT_HANDLE_FROM_FLOAT((double)res);
}

#define REDUCT_MATH_UNARY_FLOAT_IMPL(_name, _func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t* val) \
    { \
        assert(reduct != NULL); \
        reduct_handle_t fv = reduct_get_float(reduct, val); \
        return REDUCT_HANDLE_FROM_FLOAT((double)_func(REDUCT_HANDLE_TO_FLOAT(&fv))); \
    }

REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_sin, sin)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_cos, cos)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_tan, tan)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_asin, asin)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_acos, acos)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_atan, atan)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_sinh, sinh)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_cosh, cosh)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_tanh, tanh)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_asinh, asinh)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_acosh, acosh)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_atanh, atanh)

REDUCT_API reduct_handle_t reduct_atan2(struct reduct* reduct, reduct_handle_t* y, reduct_handle_t* x)
{
    assert(reduct != NULL);
    reduct_handle_t yFloatVal = reduct_get_float(reduct, y);
    reduct_handle_t xFloatVal = reduct_get_float(reduct, x);
    double yf = REDUCT_HANDLE_TO_FLOAT(&yFloatVal);
    double xf = REDUCT_HANDLE_TO_FLOAT(&xFloatVal);
    return REDUCT_HANDLE_FROM_FLOAT((double)atan2(yf, xf));
}

REDUCT_API reduct_handle_t reduct_rand(struct reduct* reduct, reduct_handle_t* minVal, reduct_handle_t* maxVal)
{
    assert(reduct != NULL);

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, minVal, maxVal, &prom);

    double r = (double)rand() / (double)RAND_MAX;

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        int64_t res = prom.a.intVal + (int64_t)(r * (prom.b.intVal - prom.a.intVal));
        return REDUCT_HANDLE_FROM_INT(res);
    }
    double res = prom.a.floatVal + (r * (prom.b.floatVal - prom.a.floatVal));
    return REDUCT_HANDLE_FROM_FLOAT(res);
}

REDUCT_API reduct_handle_t reduct_seed(struct reduct* reduct, reduct_handle_t* val)
{
    assert(reduct != NULL);
    reduct_handle_t iVal = reduct_get_int(reduct, val);
    int64_t i = REDUCT_HANDLE_TO_INT(&iVal);
    srand((unsigned int)i);
    return REDUCT_HANDLE_NIL(reduct);
}

#define REDUCT_STDLIB_WRAPPER_0(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 0, #_name ": expected 0 argument(s), got %zu", (size_t)argc); \
        (void)argv; \
        return _impl(reduct); \
    }

#define REDUCT_STDLIB_WRAPPER_1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, #_name ": expected 1 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, &argv[0]); \
    }

#define REDUCT_STDLIB_WRAPPER_2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_3(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 3, #_name ": expected 3 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], &argv[2]); \
    }

#define REDUCT_STDLIB_WRAPPER_R12(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 2, #_name ": expected 1 to 2 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, &argv[0], argc == 2 ? &argv[1] : NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_R23(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2 && argc <= 3, #_name ": expected 2 to 3 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], argc == 3 ? &argv[2] : NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_R34(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 3 && argc <= 4, #_name ": expected 3 to 4 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], &argv[2], argc == 4 ? &argv[3] : NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_ARG2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_V1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1, #_name ": expected at least 1 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, argc, argv); \
    }

REDUCT_STDLIB_WRAPPER_2(assert, reduct_assert)
REDUCT_STDLIB_WRAPPER_1(throw, reduct_throw)
REDUCT_STDLIB_WRAPPER_2(try, reduct_try)
REDUCT_STDLIB_WRAPPER_2(map, reduct_map)
REDUCT_STDLIB_WRAPPER_2(filter, reduct_filter)
REDUCT_STDLIB_WRAPPER_2(apply, reduct_apply)
REDUCT_STDLIB_WRAPPER_V1(len, reduct_len)
REDUCT_STDLIB_WRAPPER_V1(is_atom, reduct_is_atom)
REDUCT_STDLIB_WRAPPER_V1(is_int, reduct_is_int)
REDUCT_STDLIB_WRAPPER_V1(is_float, reduct_is_float)
REDUCT_STDLIB_WRAPPER_V1(is_number, reduct_is_number)
REDUCT_STDLIB_WRAPPER_V1(is_lambda, reduct_is_lambda)
REDUCT_STDLIB_WRAPPER_V1(is_native, reduct_is_native)
REDUCT_STDLIB_WRAPPER_V1(is_callable, reduct_is_callable)
REDUCT_STDLIB_WRAPPER_V1(is_list, reduct_is_list)
REDUCT_STDLIB_WRAPPER_V1(is_empty, reduct_is_empty)
REDUCT_STDLIB_WRAPPER_V1(is_nil, reduct_is_nil)

static reduct_handle_t reduct_stdlib_reduce(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2 && argc <= 3, "reduce: expected 2 to 3 argument(s), got %zu",
        (size_t)argc);
    return reduct_reduce(reduct, &argv[0], argc == 3 ? &argv[1] : NULL, argc == 3 ? &argv[2] : &argv[1]);
}

REDUCT_STDLIB_WRAPPER_R12(any, reduct_any)
REDUCT_STDLIB_WRAPPER_R12(all, reduct_all)
REDUCT_STDLIB_WRAPPER_R12(sort, reduct_sort)
REDUCT_STDLIB_WRAPPER_R12(flatten, reduct_flatten)
REDUCT_STDLIB_WRAPPER_R12(log, reduct_log)

static reduct_handle_t reduct_stdlib_range(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 3, "range: expected 1 to 3 argument(s), got %zu",
        (size_t)argc);
    reduct_handle_t* start = (argc >= 2) ? &argv[0] : NULL;
    reduct_handle_t* end = (argc == 1) ? &argv[0] : (argc >= 2 ? &argv[1] : NULL);
    reduct_handle_t* step = (argc == 3) ? &argv[2] : NULL;

    if (argc == 1)
    {
        reduct_handle_t zero = REDUCT_HANDLE_FROM_INT(0);
        return reduct_range(reduct, &zero, end, NULL);
    }

    return reduct_range(reduct, start, end, step);
}

static reduct_handle_t reduct_stdlib_concat(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return reduct_concat(reduct, argc, argv);
}

static reduct_handle_t reduct_stdlib_append(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, "append: expected at least 2 argument(s), got %zu", (size_t)argc);
    return reduct_append(reduct, argc, &argv[0]);
}

static reduct_handle_t reduct_stdlib_prepend(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, "prepend: expected at least 2 argument(s), got %zu", (size_t)argc);
    return reduct_prepend(reduct, argc, &argv[0]);
}

REDUCT_STDLIB_WRAPPER_1(first, reduct_first)
REDUCT_STDLIB_WRAPPER_1(last, reduct_last)
REDUCT_STDLIB_WRAPPER_1(rest, reduct_rest)
REDUCT_STDLIB_WRAPPER_1(init, reduct_init)
REDUCT_STDLIB_WRAPPER_1(reverse, reduct_reverse)
REDUCT_STDLIB_WRAPPER_1(unique, reduct_unique)
REDUCT_STDLIB_WRAPPER_1(keys, reduct_keys)
REDUCT_STDLIB_WRAPPER_1(values, reduct_values)
REDUCT_STDLIB_WRAPPER_R23(nth, reduct_nth)
REDUCT_STDLIB_WRAPPER_R23(slice, reduct_slice)
REDUCT_STDLIB_WRAPPER_R23(get_in, reduct_get_in)
REDUCT_STDLIB_WRAPPER_R34(assoc, reduct_assoc)
REDUCT_STDLIB_WRAPPER_ARG2(dissoc, reduct_dissoc)
REDUCT_STDLIB_WRAPPER_R34(update, reduct_update)
REDUCT_STDLIB_WRAPPER_ARG2(index_of, reduct_index_of)
REDUCT_STDLIB_WRAPPER_ARG2(contains, reduct_contains)
REDUCT_STDLIB_WRAPPER_3(replace, reduct_replace)
REDUCT_STDLIB_WRAPPER_ARG2(chunk, reduct_chunk)
REDUCT_STDLIB_WRAPPER_ARG2(find, reduct_find)
REDUCT_STDLIB_WRAPPER_3(assoc_in, reduct_assoc_in)
REDUCT_STDLIB_WRAPPER_ARG2(dissoc_in, reduct_dissoc_in)
REDUCT_STDLIB_WRAPPER_3(update_in, reduct_update_in)
REDUCT_STDLIB_WRAPPER_V1(merge, reduct_merge)
REDUCT_STDLIB_WRAPPER_V1(explode, reduct_explode)
REDUCT_STDLIB_WRAPPER_V1(implode, reduct_implode)

static reduct_handle_t reduct_stdlib_repeat(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "repeat: expected 2 argument(s), got %zu", (size_t)argc);
    return reduct_repeat(reduct, &argv[0], &argv[1]);
}

REDUCT_STDLIB_WRAPPER_ARG2(starts_with, reduct_starts_with)
REDUCT_STDLIB_WRAPPER_ARG2(ends_with, reduct_ends_with)
REDUCT_STDLIB_WRAPPER_ARG2(join, reduct_join)
REDUCT_STDLIB_WRAPPER_ARG2(split, reduct_split)

REDUCT_STDLIB_WRAPPER_1(upper, reduct_upper)
REDUCT_STDLIB_WRAPPER_1(lower, reduct_lower)
REDUCT_STDLIB_WRAPPER_1(trim, reduct_trim)
REDUCT_STDLIB_WRAPPER_1(int, reduct_get_int)
REDUCT_STDLIB_WRAPPER_1(float, reduct_get_float)

static reduct_handle_t reduct_stdlib_eval_impl(reduct_t* reduct, reduct_handle_t* arg)
{
    return reduct_eval(reduct, reduct_compile(reduct, arg));
}
REDUCT_STDLIB_WRAPPER_1(eval, reduct_stdlib_eval_impl)

static reduct_handle_t reduct_stdlib_parse_impl(reduct_t* reduct, reduct_handle_t* arg)
{
    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, arg, &str, &len);
    return reduct_parse(reduct, str, len, "<parse>");
}
REDUCT_STDLIB_WRAPPER_1(parse, reduct_stdlib_parse_impl)

REDUCT_STDLIB_WRAPPER_1(run, reduct_run)

static reduct_handle_t reduct_stdlib_import(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 3, "import: expected 1 to 3 argument(s), got %zu",
        (size_t)argc);
    return reduct_import(reduct, &argv[0], argc >= 2 ? &argv[1] : NULL, argc == 3 ? &argv[2] : NULL);
}

REDUCT_STDLIB_WRAPPER_1(read_file, reduct_read_file)
REDUCT_STDLIB_WRAPPER_2(write_file, reduct_write_file)
REDUCT_STDLIB_WRAPPER_0(read_char, reduct_read_char)
REDUCT_STDLIB_WRAPPER_0(read_line, reduct_read_line)

static reduct_handle_t reduct_stdlib_print(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return reduct_print(reduct, argc, argv);
}

static reduct_handle_t reduct_stdlib_println(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return reduct_println(reduct, argc, argv);
}

REDUCT_STDLIB_WRAPPER_1(ord, reduct_ord)
REDUCT_STDLIB_WRAPPER_1(chr, reduct_chr)

static reduct_handle_t reduct_stdlib_format(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return reduct_format(reduct, argc, argv);
}

REDUCT_STDLIB_WRAPPER_0(now, reduct_now)
REDUCT_STDLIB_WRAPPER_0(uptime, reduct_uptime)
REDUCT_STDLIB_WRAPPER_0(env, reduct_env)
REDUCT_STDLIB_WRAPPER_0(args, reduct_args)
REDUCT_STDLIB_WRAPPER_V1(min, reduct_min)
REDUCT_STDLIB_WRAPPER_V1(max, reduct_max)
REDUCT_STDLIB_WRAPPER_3(clamp, reduct_clamp)

REDUCT_STDLIB_WRAPPER_1(abs, reduct_abs)
REDUCT_STDLIB_WRAPPER_1(floor, reduct_floor)
REDUCT_STDLIB_WRAPPER_1(ceil, reduct_ceil)
REDUCT_STDLIB_WRAPPER_1(round, reduct_round)
REDUCT_STDLIB_WRAPPER_1(exp, reduct_exp)
REDUCT_STDLIB_WRAPPER_1(sqrt, reduct_sqrt)
REDUCT_STDLIB_WRAPPER_1(sin, reduct_sin)
REDUCT_STDLIB_WRAPPER_2(pow, reduct_pow)

REDUCT_STDLIB_WRAPPER_1(cos, reduct_cos)
REDUCT_STDLIB_WRAPPER_1(tan, reduct_tan)
REDUCT_STDLIB_WRAPPER_1(asin, reduct_asin)
REDUCT_STDLIB_WRAPPER_1(acos, reduct_acos)
REDUCT_STDLIB_WRAPPER_1(atan, reduct_atan)
REDUCT_STDLIB_WRAPPER_1(sinh, reduct_sinh)
REDUCT_STDLIB_WRAPPER_1(cosh, reduct_cosh)
REDUCT_STDLIB_WRAPPER_1(tanh, reduct_tanh)
REDUCT_STDLIB_WRAPPER_1(asinh, reduct_asinh)
REDUCT_STDLIB_WRAPPER_1(acosh, reduct_acosh)
REDUCT_STDLIB_WRAPPER_1(atanh, reduct_atanh)
REDUCT_STDLIB_WRAPPER_2(atan2, reduct_atan2)
REDUCT_STDLIB_WRAPPER_2(rand, reduct_rand)
REDUCT_STDLIB_WRAPPER_1(seed, reduct_seed)

REDUCT_API void reduct_stdlib_register(reduct_t* reduct, reduct_stdlib_sets_t sets)
{
    assert(reduct != NULL);

    if (sets & REDUCT_STDLIB_ERROR)
    {
        static reduct_native_t natives[] = {
            {"assert!", reduct_stdlib_assert, NULL},
            {"throw!", reduct_stdlib_throw, NULL},
            {"try", reduct_stdlib_try, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_HIGHER_ORDER)
    {
        static reduct_native_t natives[] = {
            {"map", reduct_stdlib_map, NULL},
            {"filter", reduct_stdlib_filter, NULL},
            {"reduce", reduct_stdlib_reduce, NULL},
            {"apply", reduct_stdlib_apply, NULL},
            {"any?", reduct_stdlib_any, NULL},
            {"all?", reduct_stdlib_all, NULL},
            {"sort", reduct_stdlib_sort, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_SEQUENCES)
    {
        static reduct_native_t natives[] = {
            {"len", reduct_stdlib_len, NULL},
            {"range", reduct_stdlib_range, NULL},
            {"concat", reduct_stdlib_concat, NULL},
            {"append", reduct_stdlib_append, NULL},
            {"prepend", reduct_stdlib_prepend, NULL},
            {"first", reduct_stdlib_first, NULL},
            {"last", reduct_stdlib_last, NULL},
            {"rest", reduct_stdlib_rest, NULL},
            {"init", reduct_stdlib_init, NULL},
            {"nth", reduct_stdlib_nth, NULL},
            {"assoc", reduct_stdlib_assoc, NULL},
            {"dissoc", reduct_stdlib_dissoc, NULL},
            {"update", reduct_stdlib_update, NULL},
            {"index-of", reduct_stdlib_index_of, NULL},
            {"reverse", reduct_stdlib_reverse, NULL},
            {"slice", reduct_stdlib_slice, NULL},
            {"flatten", reduct_stdlib_flatten, NULL},
            {"contains?", reduct_stdlib_contains, NULL},
            {"replace", reduct_stdlib_replace, NULL},
            {"unique", reduct_stdlib_unique, NULL},
            {"chunk", reduct_stdlib_chunk, NULL},
            {"find", reduct_stdlib_find, NULL},
            {"get-in", reduct_stdlib_get_in, NULL},
            {"assoc-in", reduct_stdlib_assoc_in, NULL},
            {"dissoc-in", reduct_stdlib_dissoc_in, NULL},
            {"update-in", reduct_stdlib_update_in, NULL},
            {"keys", reduct_stdlib_keys, NULL},
            {"values", reduct_stdlib_values, NULL},
            {"merge", reduct_stdlib_merge, NULL},
            {"explode", reduct_stdlib_explode, NULL},
            {"implode", reduct_stdlib_implode, NULL},
            {"repeat", reduct_stdlib_repeat, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_STRING)
    {
        static reduct_native_t natives[] = {
            {"starts-with?", reduct_stdlib_starts_with, NULL},
            {"ends-with?", reduct_stdlib_ends_with, NULL},
            {"contains?", reduct_stdlib_contains, NULL},
            {"replace", reduct_stdlib_replace, NULL},
            {"join", reduct_stdlib_join, NULL},
            {"split", reduct_stdlib_split, NULL},
            {"upper", reduct_stdlib_upper, NULL},
            {"lower", reduct_stdlib_lower, NULL},
            {"trim", reduct_stdlib_trim, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_INTROSPECTION)
    {
        static reduct_native_t natives[] = {
            {"atom?", reduct_stdlib_is_atom, NULL},
            {"int?", reduct_stdlib_is_int, NULL},
            {"float?", reduct_stdlib_is_float, NULL},
            {"number?", reduct_stdlib_is_number, NULL},
            {"lambda?", reduct_stdlib_is_lambda, NULL},
            {"native?", reduct_stdlib_is_native, NULL},
            {"callable?", reduct_stdlib_is_callable, NULL},
            {"list?", reduct_stdlib_is_list, NULL},
            {"empty?", reduct_stdlib_is_empty, NULL},
            {"nil?", reduct_stdlib_is_nil, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_TYPE_CASTING)
    {
        static reduct_native_t natives[] = {
            {"int", reduct_stdlib_int, NULL},
            {"float", reduct_stdlib_float, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_SYSTEM)
    {
        static reduct_native_t natives[] = {
            {"eval", reduct_stdlib_eval, NULL},
            {"parse", reduct_stdlib_parse, NULL},
            {"run", reduct_stdlib_run, NULL},
            {"import", reduct_stdlib_import, NULL},
            {"read-file!", reduct_stdlib_read_file, NULL},
            {"write-file!", reduct_stdlib_write_file, NULL},
            {"read-char!", reduct_stdlib_read_char, NULL},
            {"read-line!", reduct_stdlib_read_line, NULL},
            {"print!", reduct_stdlib_print, NULL},
            {"println!", reduct_stdlib_println, NULL},
            {"ord", reduct_stdlib_ord, NULL},
            {"chr", reduct_stdlib_chr, NULL},
            {"format", reduct_stdlib_format, NULL},
            {"now!", reduct_stdlib_now, NULL},
            {"uptime!", reduct_stdlib_uptime, NULL},
            {"env!", reduct_stdlib_env, NULL},
            {"args!", reduct_stdlib_args, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_MATH)
    {
        static reduct_native_t natives[] = {
            {"min", reduct_stdlib_min, NULL},
            {"max", reduct_stdlib_max, NULL},
            {"clamp", reduct_stdlib_clamp, NULL},
            {"abs", reduct_stdlib_abs, NULL},
            {"floor", reduct_stdlib_floor, NULL},
            {"ceil", reduct_stdlib_ceil, NULL},
            {"round", reduct_stdlib_round, NULL},
            {"pow", reduct_stdlib_pow, NULL},
            {"exp", reduct_stdlib_exp, NULL},
            {"log", reduct_stdlib_log, NULL},
            {"sqrt", reduct_stdlib_sqrt, NULL},
            {"sin", reduct_stdlib_sin, NULL},
            {"cos", reduct_stdlib_cos, NULL},
            {"tan", reduct_stdlib_tan, NULL},
            {"asin", reduct_stdlib_asin, NULL},
            {"acos", reduct_stdlib_acos, NULL},
            {"atan", reduct_stdlib_atan, NULL},
            {"atan2", reduct_stdlib_atan2, NULL},
            {"sinh", reduct_stdlib_sinh, NULL},
            {"cosh", reduct_stdlib_cosh, NULL},
            {"tanh", reduct_stdlib_tanh, NULL},
            {"asinh", reduct_stdlib_asinh, NULL},
            {"acosh", reduct_stdlib_acosh, NULL},
            {"atanh", reduct_stdlib_atanh, NULL},
            {"rand", reduct_stdlib_rand, NULL},
            {"seed!", reduct_stdlib_seed, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
}
