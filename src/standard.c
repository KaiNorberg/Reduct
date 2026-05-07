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
#include "reduct/standard.h"
#include "reduct/stringify.h"

REDUCT_API reduct_handle_t reduct_assert(reduct_t* reduct, reduct_handle_t* cond, reduct_handle_t* msg)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (!REDUCT_HANDLE_IS_TRUTHY(cond))
    {
        const char* str;
        reduct_size_t len;
        reduct_handle_atom_string(reduct, msg, &str, &len);
        REDUCT_ERROR_RUNTIME(reduct, "assert failed: %.*s", len, str);
    }

    return *cond;
}

REDUCT_API reduct_handle_t reduct_throw(reduct_t* reduct, reduct_handle_t* msg)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    const char* str;
    reduct_size_t len;
    reduct_handle_atom_string(reduct, msg, &str, &len);
    REDUCT_ERROR_RUNTIME(reduct, "%.*s", len, str);

    return reduct_handle_nil(reduct);
}

REDUCT_API reduct_handle_t reduct_try(reduct_t* reduct, reduct_handle_t* callable, reduct_handle_t* catchFn)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "try: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, catchFn), "try: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(catchFn));

    reduct_error_t* prev = reduct->error;

    reduct_error_t error = REDUCT_ERROR();
    if (REDUCT_ERROR_CATCH(&error))
    {
        reduct_handle_t msg = REDUCT_HANDLE_FROM_ATOM(
            reduct_atom_new_copy(reduct, error.message, REDUCT_STRLEN(error.message)));
        reduct_handle_t result = reduct_eval_call(reduct, *catchFn, 1, &msg);
        reduct->error = prev;
        return result;
    }

    reduct_handle_t result = reduct_eval_call(reduct, *callable, 0, REDUCT_NULL);
    reduct->error = prev;
    return result;
}

REDUCT_API reduct_handle_t reduct_map(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "map: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "map: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "filter: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "filter: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "reduce: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "reduce: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);
    reduct_handle_t accumulator = (initial != REDUCT_NULL) ? *initial : REDUCT_HANDLE_NONE;

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &listItem->list)
    {
        if (accumulator == REDUCT_HANDLE_NONE)
        {
            accumulator = entry;
            continue;
        }

        reduct_handle_t args[2] = {accumulator, entry};
        reduct_handle_t result = reduct_eval_call(reduct, *callable, 2, args);

        accumulator = result;
    }

    if (accumulator == REDUCT_HANDLE_NONE)
    {
        return reduct_handle_nil(reduct);
    }

    return accumulator;
}

REDUCT_API reduct_handle_t reduct_apply(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* callable)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "apply: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list));
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "apply: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(callable));

    reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list);
    reduct_size_t len = listItem->length;
    if (len == 0)
    {
        return reduct_eval_call(reduct, *callable, 0, REDUCT_NULL);
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
    if (fn == REDUCT_HANDLE_NONE)
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
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), #_name ": expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(list)); \
        reduct_item_t* listItem = REDUCT_HANDLE_TO_ITEM(list); \
        reduct_handle_t fn = (callable != REDUCT_NULL) ? *callable : REDUCT_HANDLE_NONE; \
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

REDUCT_ANY_ALL_IMPL(reduct_any, REDUCT_HANDLE_IS_TRUTHY(&result), REDUCT_FALSE)
REDUCT_ANY_ALL_IMPL(reduct_all, !REDUCT_HANDLE_IS_TRUTHY(&result), REDUCT_TRUE)

static void reduct_sort_merge(reduct_t* reduct, reduct_handle_t callable, reduct_handle_t* a, reduct_size_t left,
    reduct_size_t right, reduct_size_t end, reduct_handle_t* b)
{
    reduct_size_t i = left;
    reduct_size_t j = right;

    for (reduct_size_t k = left; k < end; k++)
    {
        reduct_bool_t useLeft = REDUCT_FALSE;
        if (i < right)
        {
            if (j >= end)
            {
                useLeft = REDUCT_TRUE;
            }
            else
            {
                if (callable != REDUCT_HANDLE_NONE)
                {
                    reduct_handle_t args[2] = {a[i], a[j]};
                    reduct_handle_t res = reduct_eval_call(reduct, callable, 2, args);
                    if (REDUCT_HANDLE_IS_TRUTHY(&res))
                    {
                        useLeft = REDUCT_TRUE;
                    }
                }
                else
                {
                    if (reduct_handle_compare(reduct, &a[i], &a[j]) <= 0)
                    {
                        useLeft = REDUCT_TRUE;
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(listHandle != REDUCT_NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "sort: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(listHandle));
    reduct_item_t* list = REDUCT_HANDLE_TO_ITEM(listHandle);

    reduct_handle_t callable = (callableHandle != REDUCT_NULL) ? *callableHandle : REDUCT_HANDLE_NONE;
    if (callable != REDUCT_HANDLE_NONE)
    {
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, &callable), "sort: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STR(&callable));
    }

    reduct_size_t len = list->length;
    if (len <= 1)
    {
        return *listHandle;
    }

    reduct_handle_t* a = (reduct_handle_t*)REDUCT_MALLOC(len * sizeof(reduct_handle_t));
    if (a == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    reduct_handle_t* b = (reduct_handle_t*)REDUCT_MALLOC(len * sizeof(reduct_handle_t));
    if (b == REDUCT_NULL)
    {
        REDUCT_FREE(a);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &list->list)
    {
        a[_iter.index] = entry;
    }

    reduct_handle_t* src = a;
    reduct_handle_t* dst = b;
    for (reduct_size_t width = 1; width < list->length; width *= 2)
    {
        for (reduct_size_t i = 0; i < list->length; i += 2 * width)
        {
            reduct_size_t left = i;
            reduct_size_t right = REDUCT_MIN(i + width, len);
            reduct_size_t end = REDUCT_MIN(i + 2 * width, len);
            reduct_sort_merge(reduct, callable, src, left, right, end, dst);
        }
        reduct_handle_t* temp = src;
        src = dst;
        dst = temp;
    }

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    for (reduct_size_t i = 0; i < len; i++)
    {
        reduct_list_push(reduct, resultList, src[i]);
    }

    REDUCT_FREE(a);
    REDUCT_FREE(b);
    return resultHandle;
}

static inline reduct_int64_t reduct_handle_normalize_index(reduct_t* reduct, reduct_handle_t* index,
    reduct_size_t length)
{
    reduct_handle_t nHandle = reduct_get_int(reduct, index);
    reduct_int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);
    if (n < 0)
    {
        n = (reduct_int64_t)length + n;
    }
    return n;
}

static inline void reduct_sequence_normalize_range(reduct_t* reduct, reduct_handle_t* startH, reduct_handle_t* endH,
    reduct_size_t length, reduct_size_t* outStart, reduct_size_t* outEnd)
{
    reduct_int64_t start = reduct_handle_normalize_index(reduct, startH, length);
    reduct_int64_t end;

    if (endH != REDUCT_NULL)
    {
        end = reduct_handle_normalize_index(reduct, endH, length);
    }
    else
    {
        end = (reduct_int64_t)length;
    }

    start = REDUCT_MAX(0, REDUCT_MIN(start, (reduct_int64_t)length));
    end = REDUCT_MAX(0, REDUCT_MIN(end, (reduct_int64_t)length));

    *outStart = (reduct_size_t)start;
    *outEnd = (reduct_size_t)end;
}

REDUCT_API reduct_handle_t reduct_len(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    reduct_size_t total = 0;
    for (reduct_size_t i = 0; i < argc; i++)
    {
        total += reduct_handle_as_item(reduct, &argv[i])->length;
    }
    return REDUCT_HANDLE_FROM_INT(total);
}

REDUCT_API reduct_handle_t reduct_range(struct reduct* reduct, reduct_handle_t* start, reduct_handle_t* end,
    reduct_handle_t* step)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t startH = reduct_get_int(reduct, start);
    reduct_handle_t endH = reduct_get_int(reduct, end);
    reduct_handle_t stepH = (step != REDUCT_NULL) ? reduct_get_int(reduct, step) : REDUCT_HANDLE_FROM_INT(1);

    reduct_int64_t startVal = REDUCT_HANDLE_TO_INT(&startH);
    reduct_int64_t endVal = REDUCT_HANDLE_TO_INT(&endH);
    reduct_int64_t stepVal = REDUCT_HANDLE_TO_INT(&stepH);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, stepVal != 0, "range: step must not be zero");

    reduct_size_t count = 0;
    if (stepVal > 0)
    {
        if (endVal > startVal)
        {
            count = (reduct_size_t)((endVal - startVal + stepVal - 1) / stepVal);
        }
    }
    else
    {
        if (startVal > endVal)
        {
            count = (reduct_size_t)((startVal - endVal - stepVal - 1) / -stepVal);
        }
    }

    reduct_list_t* list = reduct_list_new(reduct);
    reduct_handle_t listHandle = REDUCT_HANDLE_FROM_LIST(list);

    reduct_int64_t current = startVal;
    for (reduct_size_t i = 0; i < count; i++)
    {
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_INT(current));
        current += stepVal;
    }

    return listHandle;
}

REDUCT_API reduct_handle_t reduct_concat(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    reduct_bool_t resultIsList = REDUCT_FALSE;

    for (reduct_size_t i = 0; i < argc; i++)
    {
        if (REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            resultIsList = REDUCT_TRUE;
            continue;
        }
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(&argv[i]), "concat: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(&argv[i]));
    }

    if (resultIsList)
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

        if (argc == 0)
        {
            return newHandle;
        }

        for (reduct_size_t i = 0; i < argc; i++)
        {
            reduct_list_push(reduct, newList, argv[i]);
        }

        return newHandle;
    }

    reduct_size_t totalLen = 0;
    for (reduct_size_t i = 0; i < argc; i++)
    {
        totalLen += reduct_handle_as_item(reduct, &argv[i])->atom.length;
    }

    if (totalLen == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* first = &reduct_handle_as_item(reduct, &argv[0])->atom;
    reduct_atom_t* result = reduct_atom_superstr(reduct, first, totalLen);
    char* dst = result->string + first->length;
    for (reduct_size_t i = 1; i < argc; i++)
    {
        reduct_atom_t* src = &reduct_handle_as_item(reduct, &argv[i])->atom;
        REDUCT_MEMCPY(dst, src->string, src->length);
        dst += src->length;
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_append(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(&argv[0]), "append: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(&argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }
    reduct_list_t* list = &reduct_handle_as_item(reduct, &argv[0])->list;

    reduct_list_t* newList = reduct_list_append(reduct, list, argv[1]);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (reduct_size_t i = 2; i < argc; i++)
    {
        reduct_list_push(reduct, newList, argv[i]);
    }

    return newHandle;
}

REDUCT_API reduct_handle_t reduct_prepend(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(&argv[0]), "prepend: expected list, got %s", REDUCT_HANDLE_GET_TYPE_STR(&argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }

    reduct_list_t* list = &reduct_handle_as_item(reduct, &argv[0])->list;
    reduct_list_t* newList = reduct_list_prepend(reduct, list, argv[1]);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (reduct_size_t i = 2; i < argc; i++)
    {
        reduct_list_push(reduct, newList, argv[i]);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_edge(reduct_t* reduct, reduct_handle_t* handle, reduct_bool_t first)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length == 0)
    {
        return reduct_handle_nil(reduct);
    }

    reduct_size_t index = first ? 0 : item->length - 1;

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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "first: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_edge(reduct, handle, REDUCT_TRUE);
}

REDUCT_API reduct_handle_t reduct_last(reduct_t* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "last: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_edge(reduct, handle, REDUCT_FALSE);
}

static inline reduct_handle_t reduct_sequence_trim(reduct_t* reduct, reduct_handle_t* handle, reduct_bool_t rest)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length <= 1)
    {
        return reduct_handle_nil(reduct);
    }

    reduct_size_t start = rest ? 1 : 0;
    reduct_size_t end = rest ? item->length : item->length - 1;

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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "rest: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_trim(reduct, handle, REDUCT_TRUE);
}

REDUCT_API reduct_handle_t reduct_init(reduct_t* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "init: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));
    return reduct_sequence_trim(reduct, handle, REDUCT_FALSE);
}

REDUCT_API reduct_handle_t reduct_nth(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* index,
    reduct_handle_t* defaultVal)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "nth: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STR(handle));

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    reduct_int64_t n = reduct_handle_normalize_index(reduct, index, item->length);

    if (n < 0 || n >= (reduct_int64_t)item->length)
    {
        return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
        return reduct_list_nth(reduct, &item->list, (reduct_size_t)n);
    case REDUCT_ITEM_TYPE_ATOM:
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, (reduct_size_t)n, 1));
    default:
        return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
    }

}

REDUCT_API reduct_handle_t reduct_assoc(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* index,
    reduct_handle_t* value, reduct_handle_t* fillVal)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    reduct_int64_t n = reduct_handle_normalize_index(reduct, index, item->length);
    if (n < 0)
    {
        n = 0;
    }

    reduct_size_t targetIndex = (reduct_size_t)n;

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, targetIndex < item->length || fillVal != REDUCT_NULL, "assoc: index %zu out of bounds", targetIndex);

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
        for (reduct_size_t i = item->length; i < targetIndex; i++)
        {
            reduct_list_push(reduct, newList, *fillVal);
        }
        reduct_list_push(reduct, newList, *value);
        return newListH;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* src = &item->atom;
        reduct_atom_t* fill = (targetIndex > item->length && fillVal != REDUCT_NULL)
            ? &reduct_handle_as_item(reduct, fillVal)->atom
            : REDUCT_NULL;
        reduct_atom_t* val = &reduct_handle_as_item(reduct, value)->atom;

        reduct_size_t prefixLen = REDUCT_MIN(item->length, targetIndex);
        reduct_size_t fillCount = (targetIndex > item->length) ? targetIndex - item->length : 0;
        reduct_size_t suffixStart = targetIndex + 1;
        reduct_size_t suffixLen = (suffixStart < item->length) ? item->length - suffixStart : 0;

        reduct_size_t resultLen = prefixLen + val->length + suffixLen;
        if (fillCount > 0)
        {
            resultLen += fillCount * fill->length;
        }

        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        if (prefixLen > 0)
        {
            REDUCT_MEMCPY(dst, src->string, prefixLen);
            dst += prefixLen;
        }

        for (reduct_size_t i = 0; i < fillCount; i++)
        {
            REDUCT_MEMCPY(dst, fill->string, fill->length);
            dst += fill->length;
        }

        REDUCT_MEMCPY(dst, val->string, val->length);
        dst += val->length;

        if (suffixLen > 0)
        {
            REDUCT_MEMCPY(dst, src->string + suffixStart, suffixLen);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "assoc: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_dissoc(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* index)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    reduct_int64_t n = reduct_handle_normalize_index(reduct, index, item->length);

    if (n < 0 || n >= (reduct_int64_t)item->length)
    {
        return *handle;
    }

    reduct_size_t targetIndex = (reduct_size_t)n;

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

        reduct_size_t prefixLen = targetIndex;
        reduct_size_t suffixStart = targetIndex + 1;
        reduct_size_t suffixLen = (suffixStart < item->length) ? item->length - suffixStart : 0;
        reduct_size_t resultLen = prefixLen + suffixLen;

        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        if (prefixLen > 0)
        {
            REDUCT_MEMCPY(dst, src->string, prefixLen);
            dst += prefixLen;
        }
        if (suffixLen > 0)
        {
            REDUCT_MEMCPY(dst, src->string + suffixStart, suffixLen);
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    reduct_int64_t targetIndex = reduct_handle_normalize_index(reduct, index, item->length);
    if (targetIndex < 0)
    {
        targetIndex = 0;
    }

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, (reduct_size_t)targetIndex < item->length || fillVal != REDUCT_NULL, "update: index %lld out of bounds", (long long)targetIndex);

    reduct_handle_t currentVal = reduct_nth(reduct, handle, index, fillVal);
    reduct_handle_t newVal = reduct_eval_call(reduct, *callable, 1, &currentVal);

    return reduct_assoc(reduct, handle, index, &newVal, fillVal);
}

REDUCT_API reduct_handle_t reduct_index_of(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* target)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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
        reduct_size_t targetLen;
        reduct_handle_atom_string(reduct, target, &targetStr, &targetLen);

        if (targetLen == 0)
        {
            return REDUCT_HANDLE_FROM_INT(0);
        }

        if (targetLen <= item->length)
        {
            const char* str = item->atom.string;
            for (reduct_size_t i = 0; i <= item->length - targetLen; i++)
            {
                if (REDUCT_MEMCMP(str + i, targetStr, targetLen) == 0)
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

    return reduct_handle_nil(reduct);
}

REDUCT_API reduct_handle_t reduct_reverse(reduct_t* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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

        reduct_size_t i = item->length;
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

        for (reduct_size_t i = 0; i < item->length; i++)
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(handle != REDUCT_NULL);
    REDUCT_ASSERT(startH != REDUCT_NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    reduct_size_t start, end;
    reduct_sequence_normalize_range(reduct, startH, endH, item->length, &start, &end);

    if (start >= end)
    {
        return reduct_handle_nil(reduct);
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return *handle;
    }

    reduct_int64_t depth = -1;
    if (depthH != REDUCT_NULL)
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
                reduct_list_push_list(reduct, newList, &REDUCT_HANDLE_TO_ITEM(&flattened)->list);
            }
            else if (flattened != reduct_handle_nil(reduct))
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    reduct_handle_t index = reduct_index_of(reduct, handle, target);
    return REDUCT_HANDLE_FROM_BOOL(index != reduct_handle_nil(reduct));
}

REDUCT_API reduct_handle_t reduct_replace(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* oldVal,
    reduct_handle_t* newVal)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
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
        reduct_size_t oldLen;
        reduct_handle_atom_string(reduct, oldVal, &oldStr, &oldLen);

        const char* newStr;
        reduct_size_t newLen;
        reduct_handle_atom_string(reduct, newVal, &newStr, &newLen);

        if (oldLen == 0)
        {
            return *handle;
        }

        const char* str = item->atom.string;

        reduct_size_t matchCount = 0;
        for (reduct_size_t i = 0; i <= item->length - oldLen; )
        {
            if (REDUCT_MEMCMP(str + i, oldStr, oldLen) == 0)
            {
                matchCount++;
                i += oldLen;
            }
            else
            {
                i++;
            }
        }

        reduct_size_t resultLen = item->length - matchCount * oldLen + matchCount * newLen;
        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        reduct_size_t lastPos = 0;
        for (reduct_size_t i = 0; i <= item->length - oldLen; )
        {
            if (REDUCT_MEMCMP(str + i, oldStr, oldLen) == 0)
            {
                if (i > lastPos)
                {
                    REDUCT_MEMCPY(dst, str + lastPos, i - lastPos);
                    dst += i - lastPos;
                }
                REDUCT_MEMCPY(dst, newStr, newLen);
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
            REDUCT_MEMCPY(dst, str + lastPos, item->length - lastPos);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }
    default:
        REDUCT_ERROR_RUNTIME(reduct, "replace: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_unique(struct reduct* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
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
        reduct_bool_t found = REDUCT_FALSE;
        reduct_handle_t existing;
        REDUCT_LIST_FOR_EACH(&existing, newList)
        {
            if (reduct_handle_compare(reduct, &current, &existing) == 0)
            {
                found = REDUCT_TRUE;
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return *handle;
    }

    reduct_handle_t nHandle = reduct_get_int(reduct, sizeH);
    reduct_int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, n >= 0, "chunk: size must be non-negative, got %lld", n);

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    reduct_size_t chunkSize = (reduct_size_t)n;
    for (reduct_size_t i = 0; i < item->length; i += chunkSize)
    {
        reduct_size_t end = REDUCT_MIN(i + chunkSize, item->length);
        reduct_list_t* chunk = reduct_list_slice(reduct, &item->list, i, end);
        reduct_list_push(reduct, resultList, REDUCT_HANDLE_FROM_LIST(chunk));
    }

    return resultHandle;
}

REDUCT_API reduct_handle_t reduct_find(struct reduct* reduct, reduct_handle_t* handle, reduct_handle_t* callable)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return reduct_handle_nil(reduct);
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

    return reduct_handle_nil(reduct);
}

REDUCT_API reduct_handle_t reduct_get_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path,
    reduct_handle_t* defaultVal)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t current = *list;
    reduct_handle_t pathH = *path;

    if (REDUCT_LIKELY(!REDUCT_HANDLE_IS_LIST(&pathH)))
    {
        reduct_item_t* item = reduct_handle_as_item(reduct, &current);
        if (REDUCT_UNLIKELY(item->type != REDUCT_ITEM_TYPE_LIST))
        {
            return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
        }

        reduct_handle_t entryH = reduct_list_find_entry(reduct, item, &pathH);
        if (REDUCT_UNLIKELY(entryH == REDUCT_HANDLE_NONE))
        {
            return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
        }

        reduct_item_t* entry = REDUCT_HANDLE_TO_ITEM(&entryH);
        if (REDUCT_UNLIKELY(entry->length < 2))
        {
            return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
        }

        return reduct_list_second(reduct, &entry->list);
    }

    reduct_item_t* pathItem = REDUCT_HANDLE_TO_ITEM(&pathH);
    reduct_handle_t key;
    REDUCT_LIST_FOR_EACH(&key, &pathItem->list)
    {
        current = reduct_get_in(reduct, &current, &key, REDUCT_NULL);
        if (current == reduct_handle_nil(reduct))
        {
            return (defaultVal != REDUCT_NULL) ? *defaultVal : reduct_handle_nil(reduct);
        }
    }

    return current;
}

static reduct_handle_t reduct_assoc_key(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* key,
    reduct_handle_t* val)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_LIST, "assoc: expected list, got %s",
        reduct_item_type_str(item));

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t targetEntry = reduct_list_find_entry(reduct, item, key);
    reduct_bool_t found = REDUCT_FALSE;

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &item->list)
    {
        if (entry == targetEntry)
        {
            reduct_list_t* newEntry = reduct_list_new(reduct);
            reduct_list_push(reduct, newEntry, *key);
            reduct_list_push(reduct, newEntry, *val);
            reduct_list_push(reduct, newList, REDUCT_HANDLE_FROM_LIST(newEntry));
            found = REDUCT_TRUE;
        }
        else
        {
            reduct_list_push(reduct, newList, entry);
        }
    }

    if (!found)
    {
        reduct_list_t* newEntry = reduct_list_new(reduct);
        reduct_list_push(reduct, newEntry, *key);
        reduct_list_push(reduct, newEntry, *val);
        reduct_list_push(reduct, newList, REDUCT_HANDLE_FROM_LIST(newEntry));
    }

    return REDUCT_HANDLE_FROM_LIST(newList);
}

static reduct_handle_t reduct_dissoc_key(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* key)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    if (item->type != REDUCT_ITEM_TYPE_LIST)
    {
        return *handle;
    }

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t targetEntry = reduct_list_find_entry(reduct, item, key);

    reduct_handle_t entry;
    REDUCT_LIST_FOR_EACH(&entry, &item->list)
    {
        if (entry != targetEntry)
        {
            reduct_list_push(reduct, newList, entry);
        }
    }

    return REDUCT_HANDLE_FROM_LIST(newList);
}

REDUCT_API reduct_handle_t reduct_assoc_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path,
    reduct_handle_t* val)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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

    reduct_handle_t subItem = reduct_get_in(reduct, list, &first, REDUCT_NULL);
    if (subItem == reduct_handle_nil(reduct))
    {
        subItem = REDUCT_HANDLE_FROM_LIST(reduct_list_new(reduct));
    }

    reduct_handle_t restPath = REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &pathItem->list, 1, pathItem->length));
    reduct_handle_t updated = reduct_assoc_in(reduct, &subItem, &restPath, val);

    return reduct_assoc_key(reduct, list, &first, &updated);
}

REDUCT_API reduct_handle_t reduct_dissoc_in(reduct_t* reduct, reduct_handle_t* list, reduct_handle_t* path)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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

    reduct_handle_t subItem = reduct_get_in(reduct, list, &first, REDUCT_NULL);
    if (subItem == reduct_handle_nil(reduct))
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t currentVal = reduct_get_in(reduct, list, path, REDUCT_NULL);
    reduct_handle_t newVal = reduct_eval_call(reduct, *callable, 1, &currentVal);

    return reduct_assoc_in(reduct, list, path, &newVal);
}

static inline reduct_handle_t reduct_list_project(reduct_t* reduct, reduct_handle_t* listHandle, reduct_size_t index,
    const char* name)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "%s: expected list, got %s", name, REDUCT_HANDLE_GET_TYPE_STR(listHandle));
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    return reduct_list_project(reduct, listHandle, 0, "keys");
}

REDUCT_API reduct_handle_t reduct_values(reduct_t* reduct, reduct_handle_t* listHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    return reduct_list_project(reduct, listHandle, 1, "values");
}

REDUCT_API reduct_handle_t reduct_merge(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t result = REDUCT_HANDLE_FROM_LIST(resultList);

    for (reduct_size_t i = 0; i < argc; i++)
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

REDUCT_API reduct_handle_t reduct_explode(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    reduct_list_t* list = reduct_list_new(reduct);
    reduct_handle_t listHandle = REDUCT_HANDLE_FROM_LIST(list);

    for (reduct_size_t i = 0; i < argc; i++)
    {
        const char* str;
        reduct_size_t len;
        reduct_handle_atom_string(reduct, &argv[i], &str, &len);
        for (reduct_size_t j = 0; j < len; j++)
        {
            reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_INT((reduct_int64_t)(unsigned char)str[j]));
        }
    }

    return listHandle;
}

REDUCT_API reduct_handle_t reduct_implode(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0);

    reduct_size_t totalLen = 0;
    for (reduct_size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(&argv[i]))
        {
            continue;
        }
        totalLen += REDUCT_HANDLE_TO_ITEM(&argv[i])->length;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dst = result->string;

    for (reduct_size_t i = 0; i < argc; i++)
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t nHandle = reduct_get_int(reduct, count);
    reduct_int64_t n = REDUCT_HANDLE_TO_INT(&nHandle);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, n >= 0, "repeat: count must be non-negative, got %lld", n);

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (reduct_size_t i = 0; i < (reduct_size_t)n; i++)
    {
        reduct_list_push(reduct, newList, *handle);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_check_edge(reduct_t* reduct, reduct_handle_t* handle,
    reduct_handle_t* target, reduct_bool_t start, const char* name)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM(handle), "%s: expected list or atom, got %s", name, REDUCT_HANDLE_GET_TYPE_STR(handle));
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);

    if (item->type == REDUCT_ITEM_TYPE_LIST)
    {
        if (item->length == 0)
        {
            return REDUCT_HANDLE_FALSE();
        }
        reduct_size_t index = start ? 0 : item->length - 1;
        reduct_handle_t edge = reduct_list_nth(reduct, &item->list, index);
        return (reduct_handle_compare(reduct, &edge, target) == 0) ? REDUCT_HANDLE_TRUE() : REDUCT_HANDLE_FALSE();
    }
    else
    {
        const char *srcStr, *tgtStr;
        reduct_size_t srcLen, tgtLen;
        reduct_handle_atom_string(reduct, handle, &srcStr, &srcLen);
        reduct_handle_atom_string(reduct, target, &tgtStr, &tgtLen);

        if (tgtLen > srcLen)
        {
            return REDUCT_HANDLE_FALSE();
        }
        const char* offset = start ? srcStr : srcStr + srcLen - tgtLen;
        return (REDUCT_MEMCMP(offset, tgtStr, tgtLen) == 0) ? REDUCT_HANDLE_TRUE() : REDUCT_HANDLE_FALSE();
    }
    return REDUCT_HANDLE_FALSE();
}

REDUCT_API reduct_handle_t reduct_starts_with(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* prefix)
{
    return reduct_sequence_check_edge(reduct, handle, prefix, REDUCT_TRUE, "starts-with?");
}

REDUCT_API reduct_handle_t reduct_ends_with(reduct_t* reduct, reduct_handle_t* handle, reduct_handle_t* suffix)
{
    return reduct_sequence_check_edge(reduct, handle, suffix, REDUCT_FALSE, "ends-with?");
}

REDUCT_API reduct_handle_t reduct_join(reduct_t* reduct, reduct_handle_t* listHandle, reduct_handle_t* sepHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "join: first argument must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STR(listHandle));

    reduct_item_t* list = REDUCT_HANDLE_TO_ITEM(listHandle);
    if (list->length == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* sepAtom = &reduct_handle_as_item(reduct, sepHandle)->atom;

    reduct_size_t totalLen = 0;
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
        REDUCT_MEMCPY(dst, src->string, src->length);
        dst += src->length;
        if (_iter.index + 1 < list->length)
        {
            REDUCT_MEMCPY(dst, sepAtom->string, sepAtom->length);
            dst += sepAtom->length;
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_split(reduct_t* reduct, reduct_handle_t* srcHandle, reduct_handle_t* sepHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    const char *srcStr, *sepStr;
    reduct_size_t srcLen, sepLen;

    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);
    reduct_handle_atom_string(reduct, sepHandle, &sepStr, &sepLen);

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    if (sepLen == 0)
    {
        for (reduct_size_t i = 0; i < srcLen; i++)
        {
            reduct_list_push(reduct, resultList, REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), i, 1)));
        }
    }
    else
    {
        reduct_size_t lastPos = 0;
        for (reduct_size_t i = 0; i <= srcLen - sepLen; i++)
        {
            if (REDUCT_MEMCMP(srcStr + i, sepStr, sepLen) == 0)
            {
                reduct_list_push(reduct, resultList, REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, i - lastPos)));
                i += sepLen - 1;
                lastPos = i + 1;
            }
        }

        reduct_list_push(reduct, resultList, REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, srcLen - lastPos)));
    }

    return resultHandle;
}

static inline reduct_handle_t reduct_string_transform(reduct_t* reduct, reduct_handle_t* srcHandle, reduct_bool_t upper)
{
    const char* srcStr;
    reduct_size_t srcLen;
    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return *srcHandle;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, srcLen);
    char* dst = result->string;
    for (reduct_size_t i = 0; i < srcLen; i++)
    {
        dst[i] = upper ? REDUCT_CHAR_TO_UPPER(srcStr[i]) : REDUCT_CHAR_TO_LOWER(srcStr[i]);
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_upper(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    return reduct_string_transform(reduct, srcHandle, REDUCT_TRUE);
}

REDUCT_API reduct_handle_t reduct_lower(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    return reduct_string_transform(reduct, srcHandle, REDUCT_FALSE);
}

REDUCT_API reduct_handle_t reduct_trim(reduct_t* reduct, reduct_handle_t* srcHandle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    const char* srcStr;
    reduct_size_t srcLen;
    reduct_handle_atom_string(reduct, srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return *srcHandle;
    }

    reduct_size_t start = 0;
    while (start < srcLen && REDUCT_CHAR_IS_WHITESPACE(srcStr[start]))
    {
        start++;
    }

    if (start == srcLen)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_size_t end = srcLen - 1;
    while (end > start && REDUCT_CHAR_IS_WHITESPACE(srcStr[end]))
    {
        end--;
    }

    return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), start, end - start + 1));
}

#define REDUCT_INTROSPECTION_LOOP(_predicate) \
    do \
    { \
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        REDUCT_ASSERT(argv != REDUCT_NULL || argc == 0); \
        for (reduct_size_t i = 0; i < argc; i++) \
        { \
            if (!(_predicate)) \
            { \
                return REDUCT_HANDLE_FALSE(); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    } while (0)

#define REDUCT_INTROSPECTION_IMPL(_name, _predicate_macro) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_UNUSED(reduct); \
        REDUCT_INTROSPECTION_LOOP(_predicate_macro(&argv[i])); \
    }

REDUCT_INTROSPECTION_IMPL(reduct_is_atom, REDUCT_HANDLE_IS_ATOM)
REDUCT_INTROSPECTION_IMPL(reduct_is_int, REDUCT_HANDLE_IS_INT_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_float, REDUCT_HANDLE_IS_FLOAT_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_number, REDUCT_HANDLE_IS_NUMBER_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_lambda, REDUCT_HANDLE_IS_LAMBDA)
REDUCT_INTROSPECTION_IMPL(reduct_is_list, REDUCT_HANDLE_IS_LIST)

REDUCT_API reduct_handle_t reduct_is_native(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_NATIVE(reduct, &argv[i]));
}

REDUCT_API reduct_handle_t reduct_is_callable(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_CALLABLE(reduct, &argv[i]));
}

#define REDUCT_PREDICATE_IS_STRING(_h) (REDUCT_HANDLE_IS_ATOM(_h) && !REDUCT_HANDLE_IS_NUMBER_SHAPED(_h))
REDUCT_INTROSPECTION_IMPL(reduct_is_string, REDUCT_PREDICATE_IS_STRING)

#define REDUCT_PREDICATE_IS_EMPTY(_h) (reduct_handle_as_item(reduct, _h)->length == 0)
REDUCT_INTROSPECTION_IMPL(reduct_is_empty, REDUCT_PREDICATE_IS_EMPTY)

#define REDUCT_PREDICATE_IS_NIL(_h) (*(_h) == reduct_handle_nil(reduct))
REDUCT_INTROSPECTION_IMPL(reduct_is_nil, REDUCT_PREDICATE_IS_NIL)

REDUCT_API reduct_handle_t reduct_get_int(reduct_t* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    if (REDUCT_HANDLE_IS_INT(handle))
    {
        return *handle;
    }
    if (REDUCT_HANDLE_IS_FLOAT(handle))
    {
        return REDUCT_HANDLE_FROM_INT((reduct_int64_t)REDUCT_HANDLE_TO_FLOAT(handle));
    }

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_ATOM, "expected atom, got %s", reduct_item_type_str(item));

    if (reduct_atom_is_number(&item->atom))
    {
        return REDUCT_HANDLE_FROM_INT(reduct_atom_get_int(&item->atom));
    }

    const char* str = item->atom.string;
    reduct_size_t len = item->atom.length;
    reduct_atom_t* atom = reduct_atom_lookup(reduct, str, len, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(atom))
    {
        return REDUCT_HANDLE_FROM_INT(reduct_atom_get_int(atom));
    }

    return REDUCT_HANDLE_FROM_INT(0);
}

REDUCT_API reduct_handle_t reduct_get_float(reduct_t* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    if (REDUCT_HANDLE_IS_FLOAT(handle))
    {
        return *handle;
    }
    if (REDUCT_HANDLE_IS_INT(handle))
    {
        return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_HANDLE_TO_INT(handle));
    }

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_ATOM, "expected atom, got %s", reduct_item_type_str(item));

    if (reduct_atom_is_number(&item->atom))
    {
        return REDUCT_HANDLE_FROM_FLOAT(reduct_atom_get_float(&item->atom));
    }

    const char* str = item->atom.string;
    reduct_size_t len = item->atom.length;
    reduct_atom_t* atom = reduct_atom_lookup(reduct, str, len, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(atom))
    {
        return REDUCT_HANDLE_FROM_FLOAT(reduct_atom_get_float(atom));
    }

    return REDUCT_HANDLE_FROM_FLOAT(0.0);
}

static void reduct_path_copy(reduct_t* reduct, char* dest, const char* src, reduct_size_t len, reduct_size_t max)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, len < max, "path exceeds maximum length");
    REDUCT_MEMCPY(dest, src, len);
    dest[len] = '\0';
}

static void reduct_path_normalize(char* path, reduct_size_t* outLen)
{
    reduct_size_t len = *outLen;
    reduct_size_t w = 0;
    reduct_bool_t isAbsolute = REDUCT_FALSE;

    if (len > 0 && (path[0] == '/' || path[0] == '\\'))
    {
        isAbsolute = REDUCT_TRUE;
        w = 1;
    }

    for (reduct_size_t r = (isAbsolute ? 1 : 0); r < len; )
    {
        while (r < len && (path[r] == '/' || path[r] == '\\'))
        {
            r++;
        }
        if (r >= len) break;

        reduct_size_t compStart = r;
        while (r < len && path[r] != '/' && path[r] != '\\')
        {
            r++;
        }
        reduct_size_t compLen = r - compStart;

        if (compLen == 1 && path[compStart] == '.')
        {
            continue;
        }

        if (compLen == 2 && path[compStart] == '.' && path[compStart + 1] == '.')
        {
            if (w > (isAbsolute ? 1 : 0))
            {
                reduct_size_t lastCompStart = w;
                while (lastCompStart > 0 && path[lastCompStart - 1] != '/')
                {
                    lastCompStart--;
                }
                if (isAbsolute && lastCompStart == 0)
                {
                    lastCompStart = 1;
                }

                reduct_size_t lastLen = w - lastCompStart;
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
        for (reduct_size_t i = 0; i < compLen; i++)
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

static void reduct_resolve_path(reduct_t* reduct, const char* path, reduct_size_t pathLen, char* outPath,
    reduct_size_t maxLen, reduct_bool_t checkExistence)
{
    char normalized[REDUCT_PATH_MAX];
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, pathLen < REDUCT_PATH_MAX, "path exceeds maximum length");
    REDUCT_MEMCPY(normalized, path, pathLen);
    normalized[pathLen] = '\0';
    reduct_path_normalize(normalized, &pathLen);

    if (pathLen > 0 && (normalized[0] == '/'))
    {
        reduct_path_copy(reduct, outPath, normalized, pathLen, maxLen);
        return;
    }
    if (pathLen > 2 && ((normalized[0] >= 'a' && normalized[0] <= 'z') || (normalized[0] >= 'A' && normalized[0] <= 'Z')) && normalized[1] == ':')
    {
        reduct_path_copy(reduct, outPath, normalized, pathLen, maxLen);
        return;
    }

    if (reduct == REDUCT_NULL || reduct->frameCount == 0)
    {
        goto fallback;
    }

    reduct_input_t* input = REDUCT_NULL;
    for (reduct_size_t i = reduct->frameCount; i > 0; i--)
    {
        reduct_eval_frame_t* frame = &reduct->frames[i - 1];
        reduct_item_t* funcItem = REDUCT_CONTAINER_OF(frame->closure->function, reduct_item_t, function);
        reduct_input_t* funcInput = reduct_input_lookup(reduct, funcItem->inputId);
        if (funcInput != REDUCT_NULL && funcInput->path[0] != '\0')
        {
            input = funcInput;
            break;
        }
    }

    if (input != REDUCT_NULL)
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

        if (lastSlash != REDUCT_NULL)
        {
            reduct_size_t dirLen = (reduct_size_t)(lastSlash - input->path) + 1;
            if (dirLen + pathLen < maxLen)
            {
                REDUCT_MEMCPY(outPath, input->path, dirLen);
                REDUCT_MEMCPY(outPath + dirLen, normalized, pathLen);
                outPath[dirLen + pathLen] = '\0';

                if (!checkExistence)
                {
                    return;
                }

                reduct_stat_t st;
                if (REDUCT_STAT(outPath, &st) == 0)
                {
                    return;
                }
            }
        }
    }

    if (reduct != REDUCT_NULL && reduct->importPaths != REDUCT_NULL)
    {
        for (reduct_size_t i = 0; i < reduct->importPathCount; i++)
        {
            const char* includeDir = reduct->importPaths[i];
            reduct_size_t dirLen = REDUCT_STRLEN(includeDir);
            reduct_size_t needSep = (dirLen > 0 && includeDir[dirLen - 1] != '/') ? 1 : 0;
            reduct_size_t totalLen = dirLen + needSep + pathLen;
            if (totalLen + 1 < maxLen)
            {
                REDUCT_MEMCPY(outPath, includeDir, dirLen);
                if (needSep)
                {
                    outPath[dirLen] = '/';
                }
                REDUCT_MEMCPY(outPath + dirLen + needSep, normalized, pathLen);
                outPath[totalLen] = '\0';

                if (!checkExistence)
                {
                    return;
                }

                reduct_stat_t st;
                if (REDUCT_STAT(outPath, &st) == 0)
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    const char* str;
    reduct_size_t len;
    reduct_handle_atom_string(reduct, handle, &str, &len);

    reduct_handle_t ast = reduct_parse(reduct, str, len, "<run>");
    reduct_function_t* function = reduct_compile(reduct, &ast);
    return reduct_eval(reduct, function);
}

static void reduct_get_resolved_path(reduct_t* reduct, reduct_handle_t* pathHandle, char* outBuf)
{
    const char* pathStr;
    reduct_size_t pathLen;
    reduct_handle_atom_string(reduct, pathHandle, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, outBuf, REDUCT_PATH_MAX, REDUCT_TRUE);
}

REDUCT_API reduct_handle_t reduct_import(struct reduct* reduct, reduct_handle_t* path, reduct_handle_t* compiler, reduct_handle_t* compilerArgs)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    char libPathBuf[REDUCT_PATH_MAX];
    char pathBuf[REDUCT_PATH_MAX];
    reduct_lib_t lib;

    reduct_get_resolved_path(reduct, path, pathBuf);
    char* pathString = pathBuf;

    const char* ext = REDUCT_STRRCHR(pathBuf, '.');
    if (ext == REDUCT_NULL)
    {
        ext = "";
    }
    else
    {
        ext++;
    }

    if (REDUCT_STRCMP(ext, "c") == 0)
    {
        REDUCT_STRNCPY(libPathBuf, pathBuf, REDUCT_PATH_MAX);
        
        char* lastDot = REDUCT_STRRCHR(libPathBuf, '.');
        if (lastDot != REDUCT_NULL)
        {
            *lastDot = '\0';
        }
        REDUCT_STRNCAT(libPathBuf, ".rdt.so", REDUCT_PATH_MAX - 1);

        reduct_stat_t statLib;
        if (REDUCT_STAT(libPathBuf, &statLib) == 0)
        {
            reduct_stat_t statSrc;
            if (REDUCT_STAT(pathBuf, &statSrc) == 0)
            {
                if (REDUCT_STAT_MTIME(&statLib) >= REDUCT_STAT_MTIME(&statSrc))
                {
                    pathString = libPathBuf;
                    ext = "so";
                    goto load_shared_lib;
                }
            }
        }

        REDUCT_ERROR_RUNTIME_ASSERT(reduct, compiler != REDUCT_NULL, "import: C source import requires a compiler");

        const char* compilerStr;
        reduct_size_t compilerLen;
        reduct_handle_atom_string(reduct, compiler, &compilerStr, &compilerLen);

        const char* compilerArgsStr;
        reduct_size_t compilerArgsLen;
        if (compilerArgs != REDUCT_NULL)
        {
            reduct_handle_atom_string(reduct, compilerArgs, &compilerArgsStr, &compilerArgsLen);
        }

        reduct_size_t bufferCapacity = compilerLen + (compilerArgs != REDUCT_NULL ? compilerArgsLen : 0) + REDUCT_STRLEN(pathBuf) + REDUCT_STRLEN(libPathBuf) + 64;
        REDUCT_SCRATCH(reduct, buffer, char, bufferCapacity);
        REDUCT_SNPRINTF(buffer, bufferCapacity, "%.*s %.*s %s -shared -fPIC -o %s", (int)compilerLen, compilerStr, 
            (compilerArgs != REDUCT_NULL ? (int)compilerArgsLen : 0), (compilerArgs != REDUCT_NULL ? compilerArgsStr : ""), pathBuf, libPathBuf);

        int status = REDUCT_SYSTEM(buffer);
        REDUCT_SCRATCH_FREE(reduct, buffer);
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, status == 0, "import: compilation failed with status %d", status);

        pathString = libPathBuf;
        ext = "so";
        goto load_shared_lib;
    }

    if (REDUCT_STRCMP(ext, "so") == 0 || REDUCT_STRCMP(ext, "dll") == 0 || REDUCT_STRCMP(ext, "dylib") == 0)
    {
load_shared_lib:
        lib = REDUCT_LIB_OPEN(pathString);
        if (lib == REDUCT_NULL)
        {
            REDUCT_ERROR_RUNTIME(reduct, REDUCT_LIB_ERROR());
        }

        reduct_module_init_fn init = (reduct_module_init_fn)REDUCT_LIB_SYM(lib, REDUCT_LIB_ENTRY);
        if (init == REDUCT_NULL)
        {
            REDUCT_LIB_CLOSE(lib);
            REDUCT_ERROR_RUNTIME(reduct, "could not find %s in %s", REDUCT_LIB_ENTRY, pathString);
        }

        if (reduct->libs == REDUCT_NULL)
        {
            reduct->libCapacity = REDUCT_LIBS_INITIAL;
            reduct->libs = (reduct_lib_t*)REDUCT_MALLOC(reduct->libCapacity * sizeof(reduct_lib_t));
            if (reduct->libs == REDUCT_NULL)
            {
                REDUCT_ERROR_INTERNAL(reduct, "out of memory");
            }
        }
        else if (reduct->libCount >= reduct->libCapacity)
        {
            reduct->libCapacity *= 2;
            reduct_lib_t* newLibs = (reduct_lib_t*)REDUCT_REALLOC(reduct->libs, reduct->libCapacity * sizeof(reduct_lib_t));
            if (newLibs == REDUCT_NULL)
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    char pathBuf[REDUCT_PATH_MAX];
    reduct_get_resolved_path(reduct, path, pathBuf);

    reduct_file_t file = REDUCT_FOPEN(pathBuf, "rb");
    if (file == REDUCT_NULL)
    {
        REDUCT_ERROR_RUNTIME(reduct, "could not open %s", pathBuf);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    reduct_atom_t* atom = reduct_atom_new(reduct, (reduct_size_t)size);
    if (atom->string == REDUCT_NULL)
    {
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    if (REDUCT_FREAD(atom->string, 1, (reduct_size_t)size, file) != (reduct_size_t)size)
    {
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_RUNTIME(reduct, "could not read %s", pathBuf);
    }

    REDUCT_FCLOSE(file);
    return REDUCT_HANDLE_FROM_ATOM(atom);
}

REDUCT_API reduct_handle_t reduct_write_file(struct reduct* reduct, reduct_handle_t* path, reduct_handle_t* content)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    char pathBuf[REDUCT_PATH_MAX];
    const char* pathStr;
    reduct_size_t pathLen;
    reduct_handle_atom_string(reduct, path, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, pathBuf, REDUCT_PATH_MAX, REDUCT_FALSE);

    const char* contentStr;
    reduct_size_t contentLen;
    reduct_handle_atom_string(reduct, content, &contentStr, &contentLen);

    reduct_file_t file = REDUCT_FOPEN(pathBuf, "wb");
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, file != REDUCT_NULL, "could not open %s for writing", pathBuf);

    if (REDUCT_FWRITE(contentStr, 1, contentLen, file) != contentLen)
    {
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_RUNTIME(reduct, "could not write to %s", pathBuf);
    }

    REDUCT_FCLOSE(file);
    return *content;
}

REDUCT_API reduct_handle_t reduct_read_char(struct reduct* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    int c = REDUCT_FGETC(REDUCT_STDIN);
    if (c == EOF)
    {
        return reduct_handle_nil(reduct);
    }

    char ch = (char)c;
    return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &ch, 1));
}

REDUCT_API reduct_handle_t reduct_read_line(struct reduct* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    REDUCT_SCRATCH(reduct, buffer, char, REDUCT_SCRATCH_INITIAL);
    reduct_size_t length = 0;

    while (REDUCT_TRUE)
    {
        int c = REDUCT_FGETC(REDUCT_STDIN);
        if (c == EOF || c == '\n')
        {
            if (c == EOF && length == 0)
            {
                REDUCT_SCRATCH_FREE(reduct, buffer);
                return reduct_handle_nil(reduct);
            }
            break;
        }

        REDUCT_SCRATCH_GROW(reduct, buffer, char, length + 1);
        buffer[length++] = (char)c;
    }

    reduct_handle_t result =
        REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, buffer, length));

    REDUCT_SCRATCH_FREE(reduct, buffer);

    return result;
}

REDUCT_API reduct_handle_t reduct_print(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    for (reduct_size_t i = 0; i < argc; i++)
    {
        if (REDUCT_HANDLE_IS_INT(&argv[i]))
        {
            reduct_int64_t val = REDUCT_HANDLE_TO_INT(&argv[i]);
            REDUCT_FPRINTF(REDUCT_STDOUT, "%lld", (long long)val);
        }
        else if (REDUCT_HANDLE_IS_FLOAT(&argv[i]))
        {
            reduct_float_t val = REDUCT_HANDLE_TO_FLOAT(&argv[i]);
            REDUCT_FPRINTF(REDUCT_STDOUT, "%f", val);
        }
        else
        {
            const char* str;
            reduct_size_t len;
            reduct_handle_atom_string(reduct, &argv[i], &str, &len);
            REDUCT_FWRITE(str, 1, len, REDUCT_STDOUT);
        }

        if (i < argc - 1)
        {
            REDUCT_FWRITE(" ", 1, 1, REDUCT_STDOUT);
        }
    }
    return reduct_handle_nil(reduct);
}

REDUCT_API reduct_handle_t reduct_println(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t handle = reduct_print(reduct, argc, argv);
    REDUCT_FWRITE("\n", 1, 1, REDUCT_STDOUT);
    return handle;
}

REDUCT_API reduct_handle_t reduct_ord(struct reduct* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(handle != REDUCT_NULL);

    const char* str;
    reduct_size_t len;
    reduct_handle_atom_string(reduct, handle, &str, &len);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, len > 0, "ord: expected a non-empty atom");

    return REDUCT_HANDLE_FROM_INT((reduct_int64_t)(reduct_uint8_t)str[0]);
}

REDUCT_API reduct_handle_t reduct_chr(struct reduct* reduct, reduct_handle_t* handle)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_handle_t iVal = reduct_get_int(reduct, handle);
    reduct_int64_t val = REDUCT_HANDLE_TO_INT(&iVal);

    REDUCT_ERROR_RUNTIME_ASSERT(reduct, val >= 0 && val <= 255, "chr: expected integer 0-255, got %lld", (long long)val);

    char c = (char)(reduct_uint8_t)val;
    return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, &c, 1));
}

REDUCT_API reduct_handle_t reduct_format(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (argc == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    const char* fmtStr;
    reduct_size_t fmtLen;
    reduct_handle_atom_string(reduct, &argv[0], &fmtStr, &fmtLen);

    reduct_size_t totalLen = 0;
    reduct_size_t argIndex = 1;

    for (reduct_size_t i = 0; i < fmtLen; i++)
    {
        if (fmtStr[i] == '{')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '{')
            {
                totalLen++;
                i++;
                continue;
            }

            reduct_size_t j = i + 1;
            reduct_int64_t explicitIndex = -1;
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
                reduct_size_t idx = (explicitIndex != -1) ? (reduct_size_t)explicitIndex + 1 : argIndex++;
                if (idx >= argc)
                {
                    REDUCT_ERROR_RUNTIME(reduct, "format: argument index out of range");
                }
                if (REDUCT_HANDLE_IS_ATOM(&argv[idx]))
                {
                    const char* str;
                    reduct_size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    totalLen += len;
                }
                else
                {
                    totalLen += reduct_stringify(reduct, &argv[idx], REDUCT_NULL, 0);
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

    reduct_size_t currentPos = 0;
    argIndex = 1;

    for (reduct_size_t i = 0; i < fmtLen; i++)
    {
        if (fmtStr[i] == '{')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '{')
            {
                buffer[currentPos++] = '{';
                i++;
                continue;
            }

            reduct_size_t j = i + 1;
            reduct_int64_t explicitIndex = -1;
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
                reduct_size_t idx = (explicitIndex != -1) ? (reduct_size_t)explicitIndex + 1 : argIndex++;
                if (REDUCT_HANDLE_IS_ATOM(&argv[idx]))
                {
                    const char* str;
                    reduct_size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    REDUCT_MEMCPY(buffer + currentPos, str, len);
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

    REDUCT_ASSERT(reduct != REDUCT_NULL);

    return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_TIME());
}

REDUCT_API reduct_handle_t reduct_uptime(reduct_t* reduct)
{
    REDUCT_UNUSED(reduct);

    REDUCT_ASSERT(reduct != REDUCT_NULL);

    return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_CLOCK());
}

REDUCT_API reduct_handle_t reduct_env(struct reduct* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    extern char** environ;
    reduct_size_t count = 0;
    while (environ[count] != REDUCT_NULL)
    {
        count++;
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (reduct_size_t i = 0; i < count; i++)
    {
        char* env = environ[i];
        char* eq = strchr(env, '=');
        if (eq != REDUCT_NULL)
        {
            reduct_list_t* pair = reduct_list_new(reduct);
            reduct_list_push(reduct, pair,
                REDUCT_HANDLE_FROM_ATOM(
                    reduct_atom_new_copy(reduct, env, (reduct_size_t)(eq - env))));
            reduct_list_push(reduct, pair,
                REDUCT_HANDLE_FROM_ATOM(
                    reduct_atom_new_copy(reduct, eq + 1, REDUCT_STRLEN(eq + 1))));

            reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_LIST(pair));
        }
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

REDUCT_API reduct_handle_t reduct_args(struct reduct* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (reduct->argc == 0)
    {
        return reduct_handle_nil(reduct);
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (int i = 0; i < reduct->argc; i++)
    {
        reduct_list_push(reduct, list,
            REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, reduct->argv[i], REDUCT_STRLEN(reduct->argv[i]))));
    }

    return REDUCT_HANDLE_FROM_LIST(list);
}

#define REDUCT_MATH_MIN_MAX_IMPL(_name, _op) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        if (argc == 0) \
        { \
            return reduct_handle_nil(reduct); \
        } \
        reduct_handle_t current = argv[0]; \
        for (reduct_size_t i = 1; i < argc; i++) \
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

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
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        if (REDUCT_HANDLE_IS_INT_SHAPED(val)) \
        { \
            reduct_handle_t iVal = reduct_get_int(reduct, val); \
            reduct_int64_t i = REDUCT_HANDLE_TO_INT(&iVal); \
            return REDUCT_HANDLE_FROM_INT((reduct_int64_t)_intFunc(i)); \
        } \
        reduct_handle_t floatVal = reduct_get_float(reduct, val); \
        reduct_float_t f = REDUCT_HANDLE_TO_FLOAT(&floatVal); \
        return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)_floatFunc(f)); \
    }

#define REDUCT_INT_ABS(_x) ((_x) < 0 ? -(_x) : (_x))
REDUCT_MATH_UNARY_IMPL(reduct_abs, REDUCT_INT_ABS, REDUCT_FABS)
REDUCT_MATH_UNARY_IMPL(reduct_exp, REDUCT_EXP, REDUCT_EXP)
REDUCT_MATH_UNARY_IMPL(reduct_sqrt, REDUCT_SQRT, REDUCT_SQRT)

#define REDUCT_MATH_UNARY_TO_INT_IMPL(_name, _float_func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t* val) \
    { \
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        if (REDUCT_HANDLE_IS_INT_SHAPED(val)) \
        { \
            return reduct_get_int(reduct, val); \
        } \
        reduct_handle_t floatVal = reduct_get_float(reduct, val); \
        reduct_float_t f = REDUCT_HANDLE_TO_FLOAT(&floatVal); \
        return REDUCT_HANDLE_FROM_INT((reduct_int64_t)_float_func(f)); \
    }

REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_floor, REDUCT_FLOOR)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_ceil, REDUCT_CEIL)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_round, REDUCT_ROUND)

REDUCT_API reduct_handle_t reduct_pow(reduct_t* reduct, reduct_handle_t* base, reduct_handle_t* exp)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, base, exp, &prom);

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        return REDUCT_HANDLE_FROM_INT(
            (reduct_int64_t)REDUCT_POW((reduct_float_t)prom.a.intVal, (reduct_float_t)prom.b.intVal));
    }
    return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_POW(prom.a.floatVal, prom.b.floatVal));
}

REDUCT_API reduct_handle_t reduct_log(struct reduct* reduct, reduct_handle_t* val, reduct_handle_t* base)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (base == REDUCT_NULL)
    {
        if (REDUCT_HANDLE_IS_INT_SHAPED(val))
        {
            reduct_handle_t iVal = reduct_get_int(reduct, val);
            reduct_int64_t i = REDUCT_HANDLE_TO_INT(&iVal);
            return REDUCT_HANDLE_FROM_INT((reduct_int64_t)REDUCT_LOG(i));
        }

        reduct_handle_t floatVal = reduct_get_float(reduct, val);
        reduct_float_t f = REDUCT_HANDLE_TO_FLOAT(&floatVal);
        return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_LOG(f));
    }

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, val, base, &prom);

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        reduct_float_t res = REDUCT_LOG((reduct_float_t)prom.a.intVal) / REDUCT_LOG((reduct_float_t)prom.b.intVal);
        return REDUCT_HANDLE_FROM_INT((reduct_int64_t)res);
    }
    reduct_float_t res = REDUCT_LOG(prom.a.floatVal) / REDUCT_LOG(prom.b.floatVal);
    return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)res);
}

#define REDUCT_MATH_UNARY_FLOAT_IMPL(_name, _func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t* val) \
    { \
        REDUCT_ASSERT(reduct != REDUCT_NULL); \
        reduct_handle_t fv = reduct_get_float(reduct, val); \
        return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)_func(REDUCT_HANDLE_TO_FLOAT(&fv))); \
    }

REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_sin, REDUCT_SIN)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_cos, REDUCT_COS)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_tan, REDUCT_TAN)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_asin, REDUCT_ASIN)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_acos, REDUCT_ACOS)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_atan, REDUCT_ATAN)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_sinh, REDUCT_SINH)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_cosh, REDUCT_COSH)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_tanh, REDUCT_TANH)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_asinh, REDUCT_ASINH)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_acosh, REDUCT_ACOSH)
REDUCT_MATH_UNARY_FLOAT_IMPL(reduct_atanh, REDUCT_ATANH)

REDUCT_API reduct_handle_t reduct_atan2(struct reduct* reduct, reduct_handle_t* y, reduct_handle_t* x)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    reduct_handle_t yFloatVal = reduct_get_float(reduct, y);
    reduct_handle_t xFloatVal = reduct_get_float(reduct, x);
    reduct_float_t yf = REDUCT_HANDLE_TO_FLOAT(&yFloatVal);
    reduct_float_t xf = REDUCT_HANDLE_TO_FLOAT(&xFloatVal);
    return REDUCT_HANDLE_FROM_FLOAT((reduct_float_t)REDUCT_ATAN2(yf, xf));
}

REDUCT_API reduct_handle_t reduct_rand(struct reduct* reduct, reduct_handle_t* minVal, reduct_handle_t* maxVal)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_promotion_t prom;
    reduct_handle_promote(reduct, minVal, maxVal, &prom);

    reduct_float_t r = (reduct_float_t)REDUCT_RAND() / (reduct_float_t)REDUCT_RAND_MAX;

    if (prom.type == REDUCT_PROMOTION_TYPE_INT)
    {
        reduct_int64_t res = prom.a.intVal + (reduct_int64_t)(r * (prom.b.intVal - prom.a.intVal));
        return REDUCT_HANDLE_FROM_INT(res);
    }
    reduct_float_t res = prom.a.floatVal + (r * (prom.b.floatVal - prom.a.floatVal));
    return REDUCT_HANDLE_FROM_FLOAT(res);
}

REDUCT_API reduct_handle_t reduct_seed(struct reduct* reduct, reduct_handle_t* val)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    reduct_handle_t iVal = reduct_get_int(reduct, val);
    reduct_int64_t i = REDUCT_HANDLE_TO_INT(&iVal);
    REDUCT_SRAND((unsigned int)i);
    return reduct_handle_nil(reduct);
}

#define REDUCT_STDLIB_WRAPPER_0(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 0, #_name ": expected 0 argument(s), got %zu", (reduct_size_t)argc); \
        (void)argv; \
        return _impl(reduct); \
    }

#define REDUCT_STDLIB_WRAPPER_1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 1, #_name ": expected 1 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0]); \
    }

#define REDUCT_STDLIB_WRAPPER_2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_3(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 3, #_name ": expected 3 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], &argv[2]); \
    }

#define REDUCT_STDLIB_WRAPPER_R12(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 2, #_name ": expected 1 to 2 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], argc == 2 ? &argv[1] : REDUCT_NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_R23(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2 && argc <= 3, #_name ": expected 2 to 3 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], argc == 3 ? &argv[2] : REDUCT_NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_R34(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 3 && argc <= 4, #_name ": expected 3 to 4 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1], &argv[2], argc == 4 ? &argv[3] : REDUCT_NULL); \
    }

#define REDUCT_STDLIB_WRAPPER_ARG2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (reduct_size_t)argc); \
        return _impl(reduct, &argv[0], &argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_V1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1, #_name ": expected at least 1 argument(s), got %zu", (reduct_size_t)argc); \
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

static reduct_handle_t reduct_stdlib_reduce(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2 && argc <= 3, "reduce: expected 2 to 3 argument(s), got %zu", (reduct_size_t)argc);
    return reduct_reduce(reduct, &argv[0], argc == 3 ? &argv[1] : REDUCT_NULL, argc == 3 ? &argv[2] : &argv[1]);
}

REDUCT_STDLIB_WRAPPER_R12(any, reduct_any)
REDUCT_STDLIB_WRAPPER_R12(all, reduct_all)
REDUCT_STDLIB_WRAPPER_R12(sort, reduct_sort)
REDUCT_STDLIB_WRAPPER_R12(flatten, reduct_flatten)
REDUCT_STDLIB_WRAPPER_R12(log, reduct_log)

static reduct_handle_t reduct_stdlib_range(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 3, "range: expected 1 to 3 argument(s), got %zu", (reduct_size_t)argc);
    reduct_handle_t* start = (argc >= 2) ? &argv[0] : REDUCT_NULL;
    reduct_handle_t* end = (argc == 1) ? &argv[0] : (argc >= 2 ? &argv[1] : REDUCT_NULL);
    reduct_handle_t* step = (argc == 3) ? &argv[2] : REDUCT_NULL;

    if (argc == 1)
    {
        reduct_handle_t zero = REDUCT_HANDLE_FROM_INT(0);
        return reduct_range(reduct, &zero, end, REDUCT_NULL);
    }

    return reduct_range(reduct, start, end, step);
}

static reduct_handle_t reduct_stdlib_concat(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    return reduct_concat(reduct, argc, argv);
}

static reduct_handle_t reduct_stdlib_append(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, "append: expected at least 2 argument(s), got %zu", (reduct_size_t)argc);
    return reduct_append(reduct, argc, &argv[0]);
}

static reduct_handle_t reduct_stdlib_prepend(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 2, "prepend: expected at least 2 argument(s), got %zu", (reduct_size_t)argc);
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

static reduct_handle_t reduct_stdlib_repeat(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc == 2, "repeat: expected 2 argument(s), got %zu", (reduct_size_t)argc);
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
    reduct_size_t len;
    reduct_handle_atom_string(reduct, arg, &str, &len);
    return reduct_parse(reduct, str, len, "<parse>");
}
REDUCT_STDLIB_WRAPPER_1(parse, reduct_stdlib_parse_impl)

REDUCT_STDLIB_WRAPPER_1(run, reduct_run)

static reduct_handle_t reduct_stdlib_import(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_RUNTIME_ASSERT(reduct, argc >= 1 && argc <= 3, "import: expected 1 to 3 argument(s), got %zu", (reduct_size_t)argc);
    return reduct_import(reduct, &argv[0], argc >= 2 ? &argv[1] : REDUCT_NULL, argc == 3 ? &argv[2] : REDUCT_NULL);
}

REDUCT_STDLIB_WRAPPER_1(read_file, reduct_read_file)
REDUCT_STDLIB_WRAPPER_2(write_file, reduct_write_file)
REDUCT_STDLIB_WRAPPER_0(read_char, reduct_read_char)
REDUCT_STDLIB_WRAPPER_0(read_line, reduct_read_line)

static reduct_handle_t reduct_stdlib_print(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    return reduct_print(reduct, argc, argv);
}

static reduct_handle_t reduct_stdlib_println(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
{
    return reduct_println(reduct, argc, argv);
}

REDUCT_STDLIB_WRAPPER_1(ord, reduct_ord)
REDUCT_STDLIB_WRAPPER_1(chr, reduct_chr)

static reduct_handle_t reduct_stdlib_format(reduct_t* reduct, reduct_size_t argc, reduct_handle_t* argv)
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
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    if (sets & REDUCT_STDLIB_ERROR)
    {
        static reduct_native_t natives[] = {
            {"assert!", reduct_stdlib_assert, REDUCT_NULL},
            {"throw!", reduct_stdlib_throw, REDUCT_NULL},
            {"try", reduct_stdlib_try, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_HIGHER_ORDER)
    {
        static reduct_native_t natives[] = {
            {"map", reduct_stdlib_map, REDUCT_NULL},
            {"filter", reduct_stdlib_filter, REDUCT_NULL},
            {"reduce", reduct_stdlib_reduce, REDUCT_NULL},
            {"apply", reduct_stdlib_apply, REDUCT_NULL},
            {"any?", reduct_stdlib_any, REDUCT_NULL},
            {"all?", reduct_stdlib_all, REDUCT_NULL},
            {"sort", reduct_stdlib_sort, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_SEQUENCES)
    {
        static reduct_native_t natives[] = {
            {"len", reduct_stdlib_len, REDUCT_NULL},
            {"range", reduct_stdlib_range, REDUCT_NULL},
            {"concat", reduct_stdlib_concat, REDUCT_NULL},
            {"append", reduct_stdlib_append, REDUCT_NULL},
            {"prepend", reduct_stdlib_prepend, REDUCT_NULL},
            {"first", reduct_stdlib_first, REDUCT_NULL},
            {"last", reduct_stdlib_last, REDUCT_NULL},
            {"rest", reduct_stdlib_rest, REDUCT_NULL},
            {"init", reduct_stdlib_init, REDUCT_NULL},
            {"nth", reduct_stdlib_nth, REDUCT_NULL},
            {"assoc", reduct_stdlib_assoc, REDUCT_NULL},
            {"dissoc", reduct_stdlib_dissoc, REDUCT_NULL},
            {"update", reduct_stdlib_update, REDUCT_NULL},
            {"index-of", reduct_stdlib_index_of, REDUCT_NULL},
            {"reverse", reduct_stdlib_reverse, REDUCT_NULL},
            {"slice", reduct_stdlib_slice, REDUCT_NULL},
            {"flatten", reduct_stdlib_flatten, REDUCT_NULL},
            {"contains?", reduct_stdlib_contains, REDUCT_NULL},
            {"replace", reduct_stdlib_replace, REDUCT_NULL},
            {"unique", reduct_stdlib_unique, REDUCT_NULL},
            {"chunk", reduct_stdlib_chunk, REDUCT_NULL},
            {"find", reduct_stdlib_find, REDUCT_NULL},
            {"get-in", reduct_stdlib_get_in, REDUCT_NULL},
            {"assoc-in", reduct_stdlib_assoc_in, REDUCT_NULL},
            {"dissoc-in", reduct_stdlib_dissoc_in, REDUCT_NULL},
            {"update-in", reduct_stdlib_update_in, REDUCT_NULL},
            {"keys", reduct_stdlib_keys, REDUCT_NULL},
            {"values", reduct_stdlib_values, REDUCT_NULL},
            {"merge", reduct_stdlib_merge, REDUCT_NULL},
            {"explode", reduct_stdlib_explode, REDUCT_NULL},
            {"implode", reduct_stdlib_implode, REDUCT_NULL},
            {"repeat", reduct_stdlib_repeat, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_STRING)
    {
        static reduct_native_t natives[] = {
            {"starts-with?", reduct_stdlib_starts_with, REDUCT_NULL},
            {"ends-with?", reduct_stdlib_ends_with, REDUCT_NULL},
            {"contains?", reduct_stdlib_contains, REDUCT_NULL},
            {"replace", reduct_stdlib_replace, REDUCT_NULL},
            {"join", reduct_stdlib_join, REDUCT_NULL},
            {"split", reduct_stdlib_split, REDUCT_NULL},
            {"upper", reduct_stdlib_upper, REDUCT_NULL},
            {"lower", reduct_stdlib_lower, REDUCT_NULL},
            {"trim", reduct_stdlib_trim, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_INTROSPECTION)
    {
        static reduct_native_t natives[] = {
            {"atom?", reduct_stdlib_is_atom, REDUCT_NULL},
            {"int?", reduct_stdlib_is_int, REDUCT_NULL},
            {"float?", reduct_stdlib_is_float, REDUCT_NULL},
            {"number?", reduct_stdlib_is_number, REDUCT_NULL},
            {"lambda?", reduct_stdlib_is_lambda, REDUCT_NULL},
            {"native?", reduct_stdlib_is_native, REDUCT_NULL},
            {"callable?", reduct_stdlib_is_callable, REDUCT_NULL},
            {"list?", reduct_stdlib_is_list, REDUCT_NULL},
            {"empty?", reduct_stdlib_is_empty, REDUCT_NULL},
            {"nil?", reduct_stdlib_is_nil, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_TYPE_CASTING)
    {
        static reduct_native_t natives[] = {
            {"int", reduct_stdlib_int, REDUCT_NULL},
            {"float", reduct_stdlib_float, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_SYSTEM)
    {
        static reduct_native_t natives[] = {
            {"eval", reduct_stdlib_eval, REDUCT_NULL},
            {"parse", reduct_stdlib_parse, REDUCT_NULL},
            {"run", reduct_stdlib_run, REDUCT_NULL},
            {"import", reduct_stdlib_import, REDUCT_NULL},
            {"read-file!", reduct_stdlib_read_file, REDUCT_NULL},
            {"write-file!", reduct_stdlib_write_file, REDUCT_NULL},
            {"read-char!", reduct_stdlib_read_char, REDUCT_NULL},
            {"read-line!", reduct_stdlib_read_line, REDUCT_NULL},
            {"print!", reduct_stdlib_print, REDUCT_NULL},
            {"println!", reduct_stdlib_println, REDUCT_NULL},
            {"ord", reduct_stdlib_ord, REDUCT_NULL},
            {"chr", reduct_stdlib_chr, REDUCT_NULL},
            {"format", reduct_stdlib_format, REDUCT_NULL},
            {"now!", reduct_stdlib_now, REDUCT_NULL},
            {"uptime!", reduct_stdlib_uptime, REDUCT_NULL},
            {"env!", reduct_stdlib_env, REDUCT_NULL},
            {"args!", reduct_stdlib_args, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_MATH)
    {
        static reduct_native_t natives[] = {
            {"min", reduct_stdlib_min, REDUCT_NULL},
            {"max", reduct_stdlib_max, REDUCT_NULL},
            {"clamp", reduct_stdlib_clamp, REDUCT_NULL},
            {"abs", reduct_stdlib_abs, REDUCT_NULL},
            {"floor", reduct_stdlib_floor, REDUCT_NULL},
            {"ceil", reduct_stdlib_ceil, REDUCT_NULL},
            {"round", reduct_stdlib_round, REDUCT_NULL},
            {"pow", reduct_stdlib_pow, REDUCT_NULL},
            {"exp", reduct_stdlib_exp, REDUCT_NULL},
            {"log", reduct_stdlib_log, REDUCT_NULL},
            {"sqrt", reduct_stdlib_sqrt, REDUCT_NULL},
            {"sin", reduct_stdlib_sin, REDUCT_NULL},
            {"cos", reduct_stdlib_cos, REDUCT_NULL},
            {"tan", reduct_stdlib_tan, REDUCT_NULL},
            {"asin", reduct_stdlib_asin, REDUCT_NULL},
            {"acos", reduct_stdlib_acos, REDUCT_NULL},
            {"atan", reduct_stdlib_atan, REDUCT_NULL},
            {"atan2", reduct_stdlib_atan2, REDUCT_NULL},
            {"sinh", reduct_stdlib_sinh, REDUCT_NULL},
            {"cosh", reduct_stdlib_cosh, REDUCT_NULL},
            {"tanh", reduct_stdlib_tanh, REDUCT_NULL},
            {"asinh", reduct_stdlib_asinh, REDUCT_NULL},
            {"acosh", reduct_stdlib_acosh, REDUCT_NULL},
            {"atanh", reduct_stdlib_atanh, REDUCT_NULL},
            {"rand", reduct_stdlib_rand, REDUCT_NULL},
            {"seed!", reduct_stdlib_seed, REDUCT_NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
}