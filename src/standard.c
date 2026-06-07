#include "reduct/error.h"
#include "reduct/task.h"
#include <reduct/atom.h>
#include <reduct/build.h>
#include <reduct/char.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/emit.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/native.h>
#include <reduct/parse.h>
#include <reduct/standard.h>
#include <reduct/stringify.h>

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

REDUCT_API reduct_handle_t reduct_assert(reduct_t* reduct, reduct_handle_t state, reduct_handle_t cond,
    reduct_handle_t msg)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_TRUTHY(cond))
    {
        const char* str;
        size_t len;
        reduct_handle_atom_string(reduct, &msg, &str, &len);
        REDUCT_ERROR_THROW(reduct, "%.*s", (int)len, str);
    }

    return state;
}

REDUCT_API reduct_handle_t reduct_throw(reduct_t* reduct, reduct_handle_t msg)
{
    assert(reduct != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, &msg, &str, &len);
    REDUCT_ERROR_THROW(reduct, "%.*s", len, str);

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_try(reduct_t* reduct, reduct_handle_t callable, reduct_handle_t catchFn)
{
    assert(reduct != NULL);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "try: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(callable));
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, catchFn), "try: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(catchFn));

    size_t savedFrameCount = reduct->eval.frameCount;
    size_t savedRegCount = reduct->eval.regCount;

    reduct_error_t error = REDUCT_ERROR();
    reduct_handle_t result = REDUCT_HANDLE_NIL(reduct);

    REDUCT_ERROR_TRY(reduct, &error)
    {
        result = reduct_eval_call(reduct, callable, 0, NULL);
    }

    if (REDUCT_ERROR_SUCCESS(&error))
    {
        return result;
    }

    reduct->eval.frameCount = savedFrameCount;
    reduct->eval.regCount = savedRegCount;

    reduct_handle_t msg = REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, error.message, strlen(error.message)));
    return reduct_eval_call(reduct, catchFn, 1, &msg);
}

typedef struct
{
    reduct_task_id_t id;
    reduct_handle_t result;
    reduct_handle_t callable;
    reduct_handle_t arg;
} reduct_standard_task_t;

static void reduct_standard_worker(reduct_t* reduct, void* arg)
{
    reduct_standard_task_t* task = (reduct_standard_task_t*)arg;
    task->result = reduct_eval_call(reduct, task->callable, 1, &task->arg);
    REDUCT_HANDLE_RETAIN(reduct, task->result);
}

REDUCT_API reduct_handle_t reduct_map(reduct_t* reduct, reduct_handle_t list, reduct_handle_t callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "map: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(list));
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "map: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(callable));

    reduct_list_t* sourceList = REDUCT_HANDLE_TO_LIST(list);

    if (sourceList->length < 128)
    {
        reduct_handle_t mapped = REDUCT_HANDLE_CREATE_LIST(reduct);
        REDUCT_HANDLE_RETAIN(reduct, mapped);

        reduct_handle_t handle;
        REDUCT_LIST_FOR_EACH(&handle, sourceList)
        {
            reduct_list_push(reduct, REDUCT_HANDLE_TO_LIST(mapped), reduct_eval_call(reduct, callable, 1, &handle));
        }

        REDUCT_HANDLE_RELEASE(reduct, mapped);
        return mapped;
    }

    reduct_handle_t mapped = REDUCT_HANDLE_CREATE_LIST(reduct);
    REDUCT_HANDLE_RETAIN(reduct, mapped);
    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, sourceList->length);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, sourceList)
    {
        reduct_standard_task_t* task = &tasks[_index];
        task->callable = callable;
        task->arg = handle;
        task->id = reduct_task_create(reduct, reduct_standard_worker, task);
    }

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
        reduct_list_push(reduct, REDUCT_HANDLE_TO_LIST(mapped), tasks[i].result);
        REDUCT_HANDLE_RELEASE(reduct, tasks[i].result);
    }

    REDUCT_SCRATCH_PUT(reduct, tasks);
    REDUCT_HANDLE_RELEASE(reduct, mapped);
    return mapped;
}

REDUCT_API reduct_handle_t reduct_filter(reduct_t* reduct, reduct_handle_t list, reduct_handle_t callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "filter: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(list));
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "filter: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(callable));

    reduct_list_t* sourceList = REDUCT_HANDLE_TO_LIST(list);
    if (sourceList->length < 128)
    {
        reduct_list_t* filtered = reduct_list_new(reduct);
        reduct_list_retain(reduct, filtered);

        reduct_handle_t handle;
        REDUCT_LIST_FOR_EACH(&handle, sourceList)
        {
            reduct_handle_t result = reduct_eval_call(reduct, callable, 1, &handle);
            if (REDUCT_HANDLE_IS_TRUTHY(result))
            {
                reduct_list_push(reduct, filtered, handle);
            }
        }

        reduct_list_release(reduct, filtered);
        return REDUCT_HANDLE_FROM_LIST(filtered);
    }

    reduct_list_t* filtered = reduct_list_new(reduct);
    reduct_list_retain(reduct, filtered);

    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, sourceList->length);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, sourceList)
    {
        reduct_standard_task_t* task = &tasks[_index];
        task->callable = callable;
        task->arg = handle;
        task->id = reduct_task_create(reduct, reduct_standard_worker, task);
    }

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
    }

    REDUCT_LIST_FOR_EACH(&handle, sourceList)
    {
        if (REDUCT_HANDLE_IS_TRUTHY(tasks[_index].result))
        {
            reduct_list_push(reduct, filtered, handle);
        }
    }

    REDUCT_SCRATCH_PUT(reduct, tasks);
    reduct_list_release(reduct, filtered);
    return REDUCT_HANDLE_FROM_LIST(filtered);
}

REDUCT_API reduct_handle_t reduct_reduce(reduct_t* reduct, reduct_handle_t list, reduct_handle_t initial,
    reduct_handle_t callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "reduce: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(list));
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "reduce: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(callable));

    reduct_list_t* sourceList = REDUCT_HANDLE_TO_LIST(list);
    reduct_handle_t accumulator = initial;

    REDUCT_HANDLE_RETAIN(reduct, accumulator);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, sourceList)
    {
        if (REDUCT_HANDLE_IS_NIL(accumulator))
        {
            accumulator = handle;
            REDUCT_HANDLE_RETAIN(reduct, accumulator);
            continue;
        }

        reduct_handle_t args[2] = {accumulator, handle};
        reduct_handle_t result = reduct_eval_call(reduct, callable, 2, args);
        REDUCT_HANDLE_RETAIN(reduct, result);
        REDUCT_HANDLE_RELEASE(reduct, accumulator);

        accumulator = result;
    }

    REDUCT_HANDLE_RELEASE(reduct, accumulator);

    if (REDUCT_HANDLE_IS_NIL(accumulator))
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    return accumulator;
}

REDUCT_API reduct_handle_t reduct_apply(reduct_t* reduct, reduct_handle_t list, reduct_handle_t callable)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "apply: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(list));
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_CALLABLE(reduct, callable), "apply: expected callable, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(callable));

    reduct_list_t* sourceList = REDUCT_HANDLE_TO_LIST(list);
    size_t len = sourceList->length;
    if (len == 0)
    {
        return reduct_eval_call(reduct, callable, 0, NULL);
    }

    REDUCT_SCRATCH_GET(reduct, argv, reduct_handle_t, len);
    reduct_list_to_handles(sourceList, argv, len);
    reduct_handle_t result = reduct_eval_call(reduct, callable, len, argv);
    REDUCT_SCRATCH_PUT(reduct, argv);

    return result;
}

static inline reduct_handle_t reduct_eval_maybe_call(reduct_t* reduct, reduct_handle_t fn, reduct_handle_t arg)
{
    if (REDUCT_HANDLE_IS_NIL(fn))
    {
        return arg;
    }
    else
    {
        return reduct_eval_call(reduct, fn, 1, &arg);
    }
}

#define REDUCT_ANY_ALL_IMPL(_name, _predicate, _default) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_handle_t listHandle, reduct_handle_t callable) \
    { \
        assert(reduct != NULL); \
        REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), #_name ": expected list, got %s", \
            REDUCT_HANDLE_GET_TYPE_STRING(listHandle)); \
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listHandle); \
        reduct_handle_t fn = callable; \
        reduct_handle_t handle; \
        REDUCT_LIST_FOR_EACH(&handle, list) \
        { \
            reduct_handle_t result = reduct_eval_maybe_call(reduct, fn, handle); \
            if (_predicate) \
            { \
                return REDUCT_HANDLE_FROM_BOOL(reduct, !(_default)); \
            } \
        } \
        return REDUCT_HANDLE_FROM_BOOL(reduct, (_default)); \
    }

REDUCT_ANY_ALL_IMPL(reduct_any, REDUCT_HANDLE_IS_TRUTHY(result), false)
REDUCT_ANY_ALL_IMPL(reduct_all, !REDUCT_HANDLE_IS_TRUTHY(result), true)

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
                if (!REDUCT_HANDLE_IS_NIL(callable))
                {
                    reduct_handle_t args[2] = {a[i], a[j]};
                    reduct_handle_t res = reduct_eval_call(reduct, callable, 2, args);
                    if (REDUCT_HANDLE_IS_TRUTHY(res))
                    {
                        useLeft = true;
                    }
                }
                else
                {
                    if (reduct_handle_compare(reduct, a[i], a[j]) <= 0)
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

REDUCT_API reduct_handle_t reduct_sort(reduct_t* reduct, reduct_handle_t list, reduct_handle_t callable)
{
    assert(reduct != NULL);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "sort: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(list));
    reduct_list_t* listVal = REDUCT_HANDLE_TO_LIST(list);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_NIL(callable) || REDUCT_HANDLE_IS_CALLABLE(reduct, callable),
        "sort: expected callable, got %s", REDUCT_HANDLE_GET_TYPE_STRING(callable));

    size_t len = listVal->length;
    if (len <= 1)
    {
        return list;
    }

    REDUCT_SCRATCH_GET(reduct, a, reduct_handle_t, len);
    REDUCT_SCRATCH_GET(reduct, b, reduct_handle_t, len);

    reduct_list_to_handles(listVal, a, len);

    reduct_handle_t* src = a;
    reduct_handle_t* dst = b;
    for (size_t width = 1; width < listVal->length; width *= 2)
    {
        for (size_t i = 0; i < listVal->length; i += 2 * width)
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

    REDUCT_SCRATCH_PUT(reduct, a);
    REDUCT_SCRATCH_PUT(reduct, b);
    return resultHandle;
}

static inline int64_t reduct_handle_normalize_index(reduct_t* reduct, reduct_handle_t index, size_t length)
{
    int64_t n = reduct_handle_as_int(reduct, index);
    if (n < 0)
    {
        n = (int64_t)length + n;
    }
    return n;
}

static inline void reduct_sequence_normalize_range(reduct_t* reduct, reduct_handle_t startHandle,
    reduct_handle_t endHandle, size_t length, size_t* outStart, size_t* outEnd)
{
    int64_t start = reduct_handle_normalize_index(reduct, startHandle, length);
    int64_t end;

    if (!REDUCT_HANDLE_IS_NIL(endHandle))
    {
        end = reduct_handle_normalize_index(reduct, endHandle, length);
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
        total += reduct_handle_len(reduct, argv[i]);
    }
    return REDUCT_HANDLE_FROM_NUMBER((double)total);
}

REDUCT_API reduct_handle_t reduct_range(struct reduct* reduct, reduct_handle_t start, reduct_handle_t end,
    reduct_handle_t step)
{
    assert(reduct != NULL);

    int64_t startVal = reduct_handle_as_int(reduct, start);
    int64_t endVal = reduct_handle_as_int(reduct, end);
    int64_t stepVal = REDUCT_HANDLE_IS_NIL(step) ? 1 : reduct_handle_as_int(reduct, step);

    REDUCT_ERROR_ASSERT(reduct, stepVal != 0, "range: step must not be zero");

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
        reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_NUMBER((double)current));
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
        if (REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            resultIsList = true;
            continue;
        }
        REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM_LIKE(argv[i]), "concat: expected list or atom, got %s",
            REDUCT_HANDLE_GET_TYPE_STRING(argv[i]));
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
            if (REDUCT_HANDLE_IS_LIST(argv[i]))
            {
                reduct_list_push_list(reduct, newList, REDUCT_HANDLE_TO_LIST(argv[i]));
            }
            else
            {
                reduct_list_push(reduct, newList, argv[i]);
            }
        }

        return newHandle;
    }

    size_t totalLen = 0;
    for (size_t i = 0; i < argc; i++)
    {
        totalLen += reduct_handle_as_item(reduct, argv[i])->length;
    }

    if (totalLen == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* first = reduct_handle_as_atom(reduct, argv[0]);
    reduct_atom_t* result = reduct_atom_superstr(reduct, first, totalLen);
    char* dst = result->string + first->length;
    for (size_t i = 1; i < argc; i++)
    {
        reduct_atom_t* src = reduct_handle_as_atom(reduct, argv[i]);
        memcpy(dst, src->string, src->length);
        dst += src->length;
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_append(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(argv[0]), "append: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }
    reduct_list_t* list = &reduct_handle_as_item(reduct, argv[0])->list;

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

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(argv[0]), "prepend: expected list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(argv[0]));

    if (argc == 1)
    {
        return argv[0];
    }

    reduct_list_t* list = &reduct_handle_as_item(reduct, argv[0])->list;
    reduct_list_t* newList = reduct_list_prepend(reduct, list, argv[1]);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (size_t i = 2; i < argc; i++)
    {
        reduct_list_push(reduct, newList, argv[i]);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_edge(reduct_t* reduct, reduct_handle_t handle, bool first)
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

REDUCT_API reduct_handle_t reduct_first(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "first: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    return reduct_sequence_edge(reduct, handle, true);
}

REDUCT_API reduct_handle_t reduct_last(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "last: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    return reduct_sequence_edge(reduct, handle, false);
}

static inline reduct_handle_t reduct_sequence_trim(reduct_t* reduct, reduct_handle_t handle, bool rest)
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
        REDUCT_ERROR_THROW(reduct, "trim: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_rest(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "rest: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    return reduct_sequence_trim(reduct, handle, true);
}

REDUCT_API reduct_handle_t reduct_init(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "init: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
    return reduct_sequence_trim(reduct, handle, false);
}

REDUCT_API reduct_handle_t reduct_nth(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t defaultVal)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "nth: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);

    if (n < 0 || n >= (int64_t)item->length)
    {
        return defaultVal;
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
        return reduct_list_nth(reduct, &item->list, (size_t)n);
    case REDUCT_ITEM_TYPE_ATOM:
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, (size_t)n, 1));
    default:
        return defaultVal;
    }
}

REDUCT_API reduct_handle_t reduct_assoc(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t value, reduct_handle_t fillVal)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);
    if (n < 0)
    {
        n = 0;
    }

    size_t targetIndex = (size_t)n;

    REDUCT_ERROR_ASSERT(reduct, targetIndex < item->length || !REDUCT_HANDLE_IS_NIL(fillVal),
        "assoc: index %zu out of bounds", targetIndex);

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        if (targetIndex < item->length)
        {
            reduct_list_t* newList = reduct_list_assoc(reduct, &item->list, targetIndex, value);
            return REDUCT_HANDLE_FROM_LIST(newList);
        }

        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_handle_t newListH = REDUCT_HANDLE_FROM_LIST(newList);
        reduct_list_push_list(reduct, newList, &item->list);
        for (size_t i = item->length; i < targetIndex; i++)
        {
            reduct_list_push(reduct, newList, fillVal);
        }
        reduct_list_push(reduct, newList, value);
        return newListH;
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        reduct_atom_t* src = &item->atom;
        reduct_atom_t* fill = (targetIndex > item->length && !REDUCT_HANDLE_IS_NIL(fillVal))
            ? reduct_handle_as_atom(reduct, fillVal)
            : NULL;
        reduct_atom_t* val = reduct_handle_as_atom(reduct, value);

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
        REDUCT_ERROR_THROW(reduct, "assoc: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_dissoc(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t n = reduct_handle_normalize_index(reduct, index, item->length);
    if (n < 0 || n >= (int64_t)item->length)
    {
        return handle;
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
        REDUCT_ERROR_THROW(reduct, "dissoc: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_update(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t callable, reduct_handle_t fillVal)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    int64_t targetIndex = reduct_handle_normalize_index(reduct, index, item->length);
    if (targetIndex < 0)
    {
        targetIndex = 0;
    }

    REDUCT_ERROR_ASSERT(reduct, (size_t)targetIndex < item->length || !REDUCT_HANDLE_IS_NIL(fillVal),
        "update: index %lld out of bounds", (long long)targetIndex);

    reduct_handle_t currentVal = reduct_nth(reduct, handle, index, fillVal);
    reduct_handle_t newVal = reduct_eval_call(reduct, callable, 1, &currentVal);
    return reduct_assoc(reduct, handle, index, newVal, fillVal);
}

REDUCT_API reduct_handle_t reduct_index_of(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t target)
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
            if (reduct_handle_compare(reduct, current, target) == 0)
            {
                return REDUCT_HANDLE_FROM_NUMBER((double)_index);
            }
        }
    }
    break;
    case REDUCT_ITEM_TYPE_ATOM:
    {
        const char* targetStr;
        size_t targetLen;
        reduct_handle_atom_string(reduct, &target, &targetStr, &targetLen);

        if (targetLen == 0)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }

        if (targetLen <= item->length)
        {
            const char* str = item->atom.string;
            for (size_t i = 0; i <= item->length - targetLen; i++)
            {
                if (memcmp(str + i, targetStr, targetLen) == 0)
                {
                    return REDUCT_HANDLE_FROM_NUMBER((double)i);
                }
            }
        }
        break;
    }
    default:
        REDUCT_ERROR_THROW(reduct, "index-of: expected list or atom, got %s", reduct_item_type_str(item));
    }

    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_reverse(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    if (item->length <= 1)
    {
        return handle;
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
        REDUCT_ERROR_THROW(reduct, "reverse: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_slice(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t start,
    reduct_handle_t end)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_handle_as_item(reduct, handle);

    size_t startVal, endVal;
    reduct_sequence_normalize_range(reduct, start, end, item->length, &startVal, &endVal);
    if (startVal >= endVal)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    switch (item->type)
    {
    case REDUCT_ITEM_TYPE_LIST:
    {
        return REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, &item->list, startVal, endVal));
    }
    case REDUCT_ITEM_TYPE_ATOM:
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, &item->atom, startVal, endVal - startVal));
    }
    default:
        REDUCT_ERROR_THROW(reduct, "slice: expected list or atom, got %s", reduct_item_type_str(item));
    }
}

REDUCT_API reduct_handle_t reduct_flatten(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t depth)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return handle;
    }

    int64_t depthVal = -1;
    if (!REDUCT_HANDLE_IS_NIL(depth))
    {
        depthVal = reduct_handle_as_int(reduct, depth);
    }

    if (depthVal == 0)
    {
        return handle;
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    reduct_list_t* result = reduct_list_new(reduct);

    reduct_handle_t iter;
    REDUCT_LIST_FOR_EACH(&iter, list)
    {
        if (REDUCT_HANDLE_IS_LIST(iter))
        {
            reduct_handle_t flattened = reduct_flatten(reduct, iter, REDUCT_HANDLE_FROM_NUMBER((double)depthVal - 1));

            if (REDUCT_HANDLE_IS_LIST(flattened))
            {
                reduct_list_push_list(reduct, result, REDUCT_HANDLE_TO_LIST(flattened));
            }
            else if (!REDUCT_HANDLE_IS_NIL(flattened))
            {
                reduct_list_push(reduct, result, flattened);
            }
        }
        else
        {
            reduct_list_push(reduct, result, iter);
        }
    }

    return REDUCT_HANDLE_FROM_LIST(result);
}

REDUCT_API reduct_handle_t reduct_contains(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t target)
{
    assert(reduct != NULL);
    reduct_handle_t index = reduct_index_of(reduct, handle, target);
    return REDUCT_HANDLE_FROM_BOOL(reduct, !REDUCT_HANDLE_IS_NIL(index));
}

REDUCT_API reduct_handle_t reduct_replace(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t oldVal,
    reduct_handle_t newVal)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        reduct_list_t* result = reduct_list_new(reduct);

        reduct_handle_t handle;
        REDUCT_LIST_FOR_EACH(&handle, list)
        {
            if (reduct_handle_compare(reduct, handle, oldVal) == 0)
            {
                reduct_list_push(reduct, result, newVal);
            }
            else
            {
                reduct_list_push(reduct, result, handle);
            }
        }

        return REDUCT_HANDLE_FROM_LIST(result);
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        const char* oldStr;
        size_t oldLen;
        reduct_handle_atom_string(reduct, &oldVal, &oldStr, &oldLen);

        const char* newStr;
        size_t newLen;
        reduct_handle_atom_string(reduct, &newVal, &newStr, &newLen);

        const char* str;
        size_t len;
        reduct_handle_atom_string(reduct, &handle, &str, &len);

        if (oldLen == 0)
        {
            return handle;
        }

        size_t matchCount = 0;
        for (size_t i = 0; i <= len - oldLen;)
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

        size_t resultLen = len - matchCount * oldLen + matchCount * newLen;
        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        size_t lastPos = 0;
        for (size_t i = 0; i <= len - oldLen;)
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

        if (lastPos < len)
        {
            memcpy(dst, str + lastPos, len - lastPos);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }

    REDUCT_ERROR_THROW(reduct, "replace: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_unique(struct reduct* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return handle;
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
            if (reduct_handle_compare(reduct, current, existing) == 0)
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

REDUCT_API reduct_handle_t reduct_chunk(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t size)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return handle;
    }

    int64_t n = reduct_handle_as_int(reduct, size);
    REDUCT_ERROR_ASSERT(reduct, n >= 0, "chunk: size must be non-negative, got %lld", n);

    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
    reduct_list_t* result = reduct_list_new(reduct);

    size_t chunkSize = (size_t)n;
    for (size_t i = 0; i < item->length; i += chunkSize)
    {
        size_t end = REDUCT_MIN(i + chunkSize, item->length);
        reduct_list_t* chunk = reduct_list_slice(reduct, &item->list, i, end);
        reduct_list_push(reduct, result, REDUCT_HANDLE_FROM_LIST(chunk));
    }

    return REDUCT_HANDLE_FROM_LIST(result);
}

REDUCT_API reduct_handle_t reduct_find(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t callable)
{
    assert(reduct != NULL);
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);

    if (list->length < 128)
    {
        reduct_handle_t current;
        REDUCT_LIST_FOR_EACH(&current, list)
        {
            reduct_handle_t result = reduct_eval_call(reduct, callable, 1, &current);
            if (REDUCT_HANDLE_IS_TRUTHY(result))
            {
                return current;
            }
        }

        return REDUCT_HANDLE_NIL(reduct);
    }

    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, list->length);

    reduct_handle_t current;
    REDUCT_LIST_FOR_EACH(&current, list)
    {
        reduct_standard_task_t* task = &tasks[_index];
        task->callable = callable;
        task->arg = current;
        task->id = reduct_task_create(reduct, reduct_standard_worker, task);
    }

    for (size_t i = 0; i < list->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
    }

    REDUCT_LIST_FOR_EACH(&current, list)
    {
        reduct_standard_task_t* task = &tasks[_index];
        if (REDUCT_HANDLE_IS_TRUTHY(task->result))
        {
            REDUCT_SCRATCH_PUT(reduct, tasks);
            return current;
        }
    }

    REDUCT_SCRATCH_PUT(reduct, tasks);
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_get_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t defaultVal)
{
    assert(reduct != NULL);

    reduct_handle_t current = list;
    if (REDUCT_LIKELY(!REDUCT_HANDLE_IS_LIST(path)))
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(current)))
        {
            return defaultVal;
        }
        reduct_list_t* currentList = REDUCT_HANDLE_TO_LIST(current);

        reduct_handle_t entry = reduct_list_find_entry(reduct, currentList, path);
        if (REDUCT_UNLIKELY(REDUCT_HANDLE_IS_NIL(entry)))
        {
            return defaultVal;
        }

        reduct_item_t* entryItem = REDUCT_HANDLE_TO_ITEM(entry);
        if (REDUCT_UNLIKELY(entryItem->length < 2))
        {
            return defaultVal;
        }

        return reduct_list_second(reduct, &entryItem->list);
    }

    reduct_handle_t iter;
    REDUCT_LIST_FOR_EACH(&iter, REDUCT_HANDLE_TO_LIST(path))
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(current)))
        {
            return defaultVal;
        }
        reduct_list_t* currentList = REDUCT_HANDLE_TO_LIST(current);

        reduct_handle_t entry = reduct_list_find_entry(reduct, currentList, iter);
        if (REDUCT_UNLIKELY(REDUCT_HANDLE_IS_NIL(entry)))
        {
            return defaultVal;
        }

        reduct_item_t* entryItem = REDUCT_HANDLE_TO_ITEM(entry);
        if (REDUCT_UNLIKELY(entryItem->length < 2))
        {
            return defaultVal;
        }

        current = reduct_list_second(reduct, &entryItem->list);
    }

    return current;
}

static reduct_handle_t reduct_assoc_key(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t key,
    reduct_handle_t val)
{
    reduct_item_t* item = reduct_handle_as_item(reduct, handle);
    REDUCT_ERROR_ASSERT(reduct, item->type == REDUCT_ITEM_TYPE_LIST, "assoc: expected list, got %s",
        reduct_item_type_str(item));

    size_t index;
    if (!reduct_list_find_entry_index(reduct, &item->list, key, &index))
    {
        reduct_list_t* newList = reduct_list_new(reduct);
        reduct_list_push_list(reduct, newList, &item->list);
        reduct_list_t* newEntry = reduct_list_new(reduct);
        reduct_list_push(reduct, newEntry, key);
        reduct_list_push(reduct, newEntry, val);
        reduct_list_push(reduct, newList, REDUCT_HANDLE_FROM_LIST(newEntry));
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    reduct_list_t* newEntry = reduct_list_new(reduct);
    reduct_list_push(reduct, newEntry, key);
    reduct_list_push(reduct, newEntry, val);
    reduct_list_t* newList = reduct_list_assoc(reduct, &item->list, index, REDUCT_HANDLE_FROM_LIST(newEntry));
    return REDUCT_HANDLE_FROM_LIST(newList);
}

static reduct_handle_t reduct_dissoc_key(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t key)
{
    if (!REDUCT_HANDLE_IS_LIST(handle))
    {
        return handle;
    }
    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);

    size_t index;
    if (!reduct_list_find_entry_index(reduct, list, key, &index))
    {
        return handle;
    }

    reduct_list_t* newList = reduct_list_dissoc(reduct, list, index);
    return REDUCT_HANDLE_FROM_LIST(newList);
}

REDUCT_API reduct_handle_t reduct_assoc_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t val)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(path))
    {
        return reduct_assoc_key(reduct, list, path, val);
    }

    reduct_list_t* pathList = REDUCT_HANDLE_TO_LIST(path);
    if (pathList->length == 0)
    {
        return val;
    }

    reduct_handle_t first = reduct_list_first(reduct, pathList);
    if (pathList->length == 1)
    {
        return reduct_assoc_key(reduct, list, first, val);
    }

    reduct_handle_t subItem = reduct_get_in(reduct, list, first, REDUCT_HANDLE_NIL(reduct));
    reduct_handle_t restPath = REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, pathList, 1, pathList->length));
    reduct_handle_t updated = reduct_assoc_in(reduct, subItem, restPath, val);

    return reduct_assoc_key(reduct, list, first, updated);
}

REDUCT_API reduct_handle_t reduct_dissoc_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_LIST(path))
    {
        return reduct_dissoc_key(reduct, list, path);
    }

    reduct_list_t* pathList = REDUCT_HANDLE_TO_LIST(path);
    if (pathList->length == 0)
    {
        return list;
    }

    reduct_handle_t first = reduct_list_first(reduct, pathList);
    if (pathList->length == 1)
    {
        return reduct_dissoc_key(reduct, list, first);
    }

    reduct_handle_t subItem = reduct_get_in(reduct, list, first, REDUCT_HANDLE_NIL(reduct));
    if (REDUCT_HANDLE_IS_NIL(subItem))
    {
        return list;
    }

    reduct_handle_t restPath = REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, pathList, 1, pathList->length));
    reduct_handle_t updated = reduct_dissoc_in(reduct, subItem, restPath);

    return reduct_assoc_key(reduct, list, first, updated);
}

REDUCT_API reduct_handle_t reduct_update_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t callable)
{
    assert(reduct != NULL);

    reduct_handle_t currentVal = reduct_get_in(reduct, list, path, REDUCT_HANDLE_NIL(reduct));
    reduct_handle_t newVal = reduct_eval_call(reduct, callable, 1, &currentVal);

    return reduct_assoc_in(reduct, list, path, newVal);
}

static inline reduct_handle_t reduct_list_project(reduct_t* reduct, reduct_handle_t list, size_t index,
    const char* name)
{
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(list), "%s: expected list, got %s", name,
        REDUCT_HANDLE_GET_TYPE_STRING(list));

    reduct_list_t* resultList = reduct_list_new(reduct);

    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, REDUCT_HANDLE_TO_LIST(list))
    {
        if (REDUCT_HANDLE_IS_LIST(handle))
        {
            reduct_item_t* childItem = REDUCT_HANDLE_TO_ITEM(handle);
            if (childItem->length > index)
            {
                reduct_list_push(reduct, resultList, reduct_list_nth(reduct, &childItem->list, index));
            }
        }
    }

    return REDUCT_HANDLE_FROM_LIST(resultList);
}

REDUCT_API reduct_handle_t reduct_keys(reduct_t* reduct, reduct_handle_t listHandle)
{
    assert(reduct != NULL);
    return reduct_list_project(reduct, listHandle, 0, "keys");
}

REDUCT_API reduct_handle_t reduct_values(reduct_t* reduct, reduct_handle_t listHandle)
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
        if (!REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            continue;
        }

        reduct_handle_t entry;
        REDUCT_LIST_FOR_EACH(&entry, REDUCT_HANDLE_TO_LIST(argv[i]))
        {
            reduct_handle_t key, val;
            if (reduct_list_get_entry(reduct, entry, &key, &val))
            {
                reduct_handle_t next = reduct_assoc_in(reduct, result, key, val);
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
            reduct_list_push(reduct, list, REDUCT_HANDLE_FROM_NUMBER((double)(int64_t)(unsigned char)str[j]));
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
        if (!REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            continue;
        }
        totalLen += REDUCT_HANDLE_TO_LIST(argv[i])->length;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dest = result->string;

    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            continue;
        }

        reduct_handle_t val;
        REDUCT_LIST_FOR_EACH(&val, REDUCT_HANDLE_TO_LIST(argv[i]))
        {
            *dest++ = reduct_handle_as_int(reduct, val);
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_repeat(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t count)
{
    assert(reduct != NULL);

    int64_t n = reduct_handle_as_int(reduct, count);
    REDUCT_ERROR_ASSERT(reduct, n >= 0, "repeat: count must be non-negative, got %lld", n);

    reduct_list_t* newList = reduct_list_new(reduct);
    reduct_handle_t newHandle = REDUCT_HANDLE_FROM_LIST(newList);

    for (size_t i = 0; i < (size_t)n; i++)
    {
        reduct_list_push(reduct, newList, handle);
    }

    return newHandle;
}

static inline reduct_handle_t reduct_sequence_check_edge(reduct_t* reduct, reduct_handle_t handle,
    reduct_handle_t target, bool start, const char* name)
{
    assert(reduct != NULL);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(handle) || REDUCT_HANDLE_IS_ATOM_LIKE(handle),
        "%s: expected list or atom, got %s", name, REDUCT_HANDLE_GET_TYPE_STRING(handle));

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        if (list->length == 0)
        {
            return REDUCT_HANDLE_FALSE(reduct);
        }
        size_t index = start ? 0 : list->length - 1;
        reduct_handle_t edge = reduct_list_nth(reduct, list, index);
        return REDUCT_HANDLE_FROM_BOOL(reduct, reduct_handle_compare(reduct, edge, target) == 0);
    }

    const char *srcStr, *tgtStr;
    size_t srcLen, tgtLen;
    reduct_handle_atom_string(reduct, &handle, &srcStr, &srcLen);
    reduct_handle_atom_string(reduct, &target, &tgtStr, &tgtLen);

    if (tgtLen > srcLen)
    {
        return REDUCT_HANDLE_FALSE(reduct);
    }
    const char* offset = start ? srcStr : srcStr + srcLen - tgtLen;
    return REDUCT_HANDLE_FROM_BOOL(reduct, memcmp(offset, tgtStr, tgtLen) == 0);
}

REDUCT_API reduct_handle_t reduct_starts_with(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t prefix)
{
    return reduct_sequence_check_edge(reduct, handle, prefix, true, "starts-with?");
}

REDUCT_API reduct_handle_t reduct_ends_with(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t suffix)
{
    return reduct_sequence_check_edge(reduct, handle, suffix, false, "ends-with?");
}

REDUCT_API reduct_handle_t reduct_join(reduct_t* reduct, reduct_handle_t listHandle, reduct_handle_t sepHandle)
{
    assert(reduct != NULL);

    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_LIST(listHandle), "join: first argument must be a list, got %s",
        REDUCT_HANDLE_GET_TYPE_STRING(listHandle));

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listHandle);
    if (list->length == 0)
    {
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_new(reduct, 0));
    }

    reduct_atom_t* sepAtom = reduct_handle_as_atom(reduct, sepHandle);

    size_t totalLen = 0;
    reduct_handle_t handle;
    REDUCT_LIST_FOR_EACH(&handle, list)
    {
        totalLen += reduct_handle_as_atom(reduct, handle)->length;
        if (_index + 1 < list->length)
        {
            totalLen += sepAtom->length;
        }
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dst = result->string;

    REDUCT_LIST_FOR_EACH(&handle, list)
    {
        reduct_atom_t* src = reduct_handle_as_atom(reduct, handle);
        memcpy(dst, src->string, src->length);
        dst += src->length;
        if (_index + 1 < list->length)
        {
            memcpy(dst, sepAtom->string, sepAtom->length);
            dst += sepAtom->length;
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_split(reduct_t* reduct, reduct_handle_t srcHandle, reduct_handle_t sepHandle)
{
    assert(reduct != NULL);

    const char *srcStr, *sepStr;
    size_t srcLen, sepLen;

    reduct_handle_atom_string(reduct, &srcHandle, &srcStr, &srcLen);
    reduct_handle_atom_string(reduct, &sepHandle, &sepStr, &sepLen);

    reduct_list_t* resultList = reduct_list_new(reduct);
    reduct_handle_t resultHandle = REDUCT_HANDLE_FROM_LIST(resultList);

    if (sepLen == 0)
    {
        for (size_t i = 0; i < srcLen; i++)
        {
            reduct_list_push(reduct, resultList,
                REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), i, 1)));
        }

        return resultHandle;
    }

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

    return resultHandle;
}

static inline reduct_handle_t reduct_string_transform(reduct_t* reduct, reduct_handle_t srcHandle, bool upper)
{
    const char* srcStr;
    size_t srcLen;
    reduct_handle_atom_string(reduct, &srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return srcHandle;
    }

    reduct_atom_t* result = reduct_atom_new(reduct, srcLen);
    char* dst = result->string;
    for (size_t i = 0; i < srcLen; i++)
    {
        dst[i] = upper ? REDUCT_CHAR_TO_UPPER(srcStr[i]) : REDUCT_CHAR_TO_LOWER(srcStr[i]);
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_upper(reduct_t* reduct, reduct_handle_t srcHandle)
{
    assert(reduct != NULL);
    return reduct_string_transform(reduct, srcHandle, true);
}

REDUCT_API reduct_handle_t reduct_lower(reduct_t* reduct, reduct_handle_t srcHandle)
{
    assert(reduct != NULL);
    return reduct_string_transform(reduct, srcHandle, false);
}

REDUCT_API reduct_handle_t reduct_trim(reduct_t* reduct, reduct_handle_t srcHandle)
{
    assert(reduct != NULL);

    const char* srcStr;
    size_t srcLen;
    reduct_handle_atom_string(reduct, &srcHandle, &srcStr, &srcLen);

    if (srcLen == 0)
    {
        return srcHandle;
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
                return REDUCT_HANDLE_FALSE(reduct); \
            } \
        } \
        return REDUCT_HANDLE_TRUE(); \
    } while (0)

#define REDUCT_INTROSPECTION_IMPL(_name, _predicate_macro) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_UNUSED(reduct); \
        REDUCT_INTROSPECTION_LOOP(_predicate_macro(argv[i])); \
    }

REDUCT_INTROSPECTION_IMPL(reduct_is_atom, REDUCT_HANDLE_IS_ATOM_LIKE)
REDUCT_INTROSPECTION_IMPL(reduct_is_number, REDUCT_HANDLE_IS_NUMBER_SHAPED)
REDUCT_INTROSPECTION_IMPL(reduct_is_lambda, REDUCT_HANDLE_IS_LAMBDA)
REDUCT_INTROSPECTION_IMPL(reduct_is_list, REDUCT_HANDLE_IS_LIST)

REDUCT_API reduct_handle_t reduct_is_native(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_NATIVE(reduct, argv[i]));
}

REDUCT_API reduct_handle_t reduct_is_callable(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_INTROSPECTION_LOOP(REDUCT_HANDLE_IS_CALLABLE(reduct, argv[i]));
}

#define REDUCT_PREDICATE_IS_EMPTY(_h) (reduct_handle_as_item(reduct, _h)->length == 0)
REDUCT_INTROSPECTION_IMPL(reduct_is_empty, REDUCT_PREDICATE_IS_EMPTY)

REDUCT_INTROSPECTION_IMPL(reduct_is_nil, REDUCT_HANDLE_IS_NIL)

REDUCT_API reduct_handle_t reduct_run(struct reduct* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, &handle, &str, &len);

    reduct_handle_t ast = reduct_parse(reduct, str, len, "<run>");
    return reduct_eval(reduct, ast);
}

static void reduct_get_resolved_path(reduct_t* reduct, reduct_handle_t pathHandle, char* outBuf)
{
    const char* pathStr;
    size_t pathLen;
    reduct_handle_atom_string(reduct, &pathHandle, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, outBuf, REDUCT_PATH_MAX, true);
}

REDUCT_API reduct_handle_t reduct_import(struct reduct* reduct, reduct_handle_t path, reduct_handle_t compiler,
    reduct_handle_t compilerArgs)
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

        const char* compilerStr;
        size_t compilerLen;
        reduct_handle_atom_string(reduct, &compiler, &compilerStr, &compilerLen);

        const char* compilerArgsStr;
        size_t compilerArgsLen;
        if (!REDUCT_HANDLE_IS_NIL(compilerArgs))
        {
            reduct_handle_atom_string(reduct, &compilerArgs, &compilerArgsStr, &compilerArgsLen);
        }

        size_t bufferCapacity = compilerLen + (!REDUCT_HANDLE_IS_NIL(compilerArgs) ? compilerArgsLen : 0) +
            strlen(pathBuf) + strlen(libPathBuf) + 64;
        REDUCT_SCRATCH_GET(reduct, buffer, char, bufferCapacity);
        snprintf(buffer, bufferCapacity, "%.*s %.*s %s -shared -fPIC -o %s", (int)compilerLen, compilerStr,
            (!REDUCT_HANDLE_IS_NIL(compilerArgs) ? (int)compilerArgsLen : 0),
            (!REDUCT_HANDLE_IS_NIL(compilerArgs) ? compilerArgsStr : ""), pathBuf, libPathBuf);

        int status = system(buffer);
        REDUCT_SCRATCH_PUT(reduct, buffer);
        REDUCT_ERROR_ASSERT(reduct, status == 0, "import: compilation failed with status %d", status);

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
            REDUCT_ERROR_THROW(reduct, REDUCT_LIB_ERROR());
        }

        reduct_module_init_fn init = (reduct_module_init_fn)REDUCT_LIB_SYM(lib, REDUCT_LIB_ENTRY);
        if (init == NULL)
        {
            REDUCT_LIB_CLOSE(lib);
            REDUCT_ERROR_THROW(reduct, "could not find %s in %s", REDUCT_LIB_ENTRY, pathString);
        }

        reduct_global_lib_add(reduct, lib);
        return init(reduct);
    }

    return reduct_eval_file(reduct, pathString, reduct->global->optimize.lastFlags);
}

REDUCT_API reduct_handle_t reduct_read_file(struct reduct* reduct, reduct_handle_t path)
{
    assert(reduct != NULL);
    char pathBuf[REDUCT_PATH_MAX];
    reduct_get_resolved_path(reduct, path, pathBuf);

    FILE* file = fopen(pathBuf, "rb");
    if (file == NULL)
    {
        REDUCT_ERROR_THROW(reduct, "could not open %s", pathBuf);
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
        REDUCT_ERROR_THROW(reduct, "could not read %s", pathBuf);
    }

    fclose(file);
    return REDUCT_HANDLE_FROM_ATOM(atom);
}

REDUCT_API reduct_handle_t reduct_write_file(struct reduct* reduct, reduct_handle_t state, reduct_handle_t path,
    reduct_handle_t content)
{
    assert(reduct != NULL);

    char pathBuf[REDUCT_PATH_MAX];
    const char* pathStr;
    size_t pathLen;
    reduct_handle_atom_string(reduct, &path, &pathStr, &pathLen);
    reduct_resolve_path(reduct, pathStr, pathLen, pathBuf, REDUCT_PATH_MAX, false);

    const char* contentStr;
    size_t contentLen;
    reduct_handle_atom_string(reduct, &content, &contentStr, &contentLen);

    FILE* file = fopen(pathBuf, "wb");
    REDUCT_ERROR_ASSERT(reduct, file != NULL, "could not open %s for writing", pathBuf);

    if (fwrite(contentStr, 1, contentLen, file) != contentLen)
    {
        fclose(file);
        REDUCT_ERROR_THROW(reduct, "could not write to %s", pathBuf);
    }

    fclose(file);
    return state;
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

    REDUCT_SCRATCH_GET(reduct, buffer, char, REDUCT_SCRATCH_INITIAL);
    size_t length = 0;

    while (true)
    {
        int c = fgetc(stdin);
        if (c == EOF || c == '\n')
        {
            if (c == EOF && length == 0)
            {
                REDUCT_SCRATCH_PUT(reduct, buffer);
                return REDUCT_HANDLE_NIL(reduct);
            }
            break;
        }

        REDUCT_SCRATCH_GROW(reduct, buffer, char, length + 1);
        buffer[length++] = (char)c;
    }

    reduct_handle_t result = REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, buffer, length));

    REDUCT_SCRATCH_PUT(reduct, buffer);

    return result;
}

REDUCT_API reduct_handle_t reduct_print(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    REDUCT_ERROR_ASSERT(reduct, argc > 0, "print: expected at least one argument");

    for (size_t i = 1; i < argc; i++)
    {
        if (REDUCT_HANDLE_IS_NUMBER(argv[i]))
        {
            fprintf(stdout, "%f", REDUCT_HANDLE_TO_NUMBER(argv[i]));
        }
        else if (REDUCT_HANDLE_IS_ATOM(argv[i]) && REDUCT_HANDLE_TO_ATOM(argv[i])->flags & REDUCT_ATOM_FLAG_QUOTED)
        {
            const char* str;
            size_t len;
            reduct_handle_atom_string(reduct, &argv[i], &str, &len);
            fwrite(str, 1, len, stdout);
        }
        else
        {
            size_t len = reduct_stringify(reduct, argv[i], NULL, 0);
            REDUCT_SCRATCH_GET(reduct, buffer, char, len + 1);
            reduct_stringify(reduct, argv[i], buffer, len + 1);
            fwrite(buffer, 1, len, stdout);
            REDUCT_SCRATCH_PUT(reduct, buffer);
        }

        if (i < argc - 1)
        {
            fwrite(" ", 1, 1, stdout);
        }
    }
    return argv[0];
}

REDUCT_API reduct_handle_t reduct_println(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);

    reduct_handle_t handle = reduct_print(reduct, argc, argv);
    fwrite("\n", 1, 1, stdout);
    return handle;
}

REDUCT_API reduct_handle_t reduct_ord(struct reduct* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, &handle, &str, &len);

    REDUCT_ERROR_ASSERT(reduct, len > 0, "ord: expected a non-empty atom");

    return REDUCT_HANDLE_FROM_NUMBER((double)(int64_t)(uint8_t)str[0]);
}

REDUCT_API reduct_handle_t reduct_chr(struct reduct* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    int64_t val = reduct_handle_as_int(reduct, handle);
    REDUCT_ERROR_ASSERT(reduct, val >= 0 && val <= 255, "chr: expected integer 0-255, got %lld", (long long)val);

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
                    REDUCT_ERROR_THROW(reduct, "format: argument index out of range");
                }

                if (REDUCT_HANDLE_IS_ATOM_LIKE(argv[idx]))
                {
                    const char* str;
                    size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    totalLen += len;
                }
                else
                {
                    totalLen += reduct_stringify(reduct, argv[idx], NULL, 0);
                }

                i = j;
                continue;
            }

            REDUCT_ERROR_THROW(reduct, "format: invalid format specifier");
        }
        else if (fmtStr[i] == '}')
        {
            if (i + 1 < fmtLen && fmtStr[i + 1] == '}')
            {
                totalLen++;
                i++;
                continue;
            }

            REDUCT_ERROR_THROW(reduct, "format: unmatched '}'");
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
                if (REDUCT_HANDLE_IS_ATOM_LIKE(argv[idx]))
                {
                    const char* str;
                    size_t len;
                    reduct_handle_atom_string(reduct, &argv[idx], &str, &len);
                    memcpy(buffer + currentPos, str, len);
                    currentPos += len;
                }
                else
                {
                    currentPos += reduct_stringify(reduct, argv[idx], buffer + currentPos, totalLen - currentPos + 1);
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

    return REDUCT_HANDLE_FROM_NUMBER((double)time(NULL));
}

REDUCT_API reduct_handle_t reduct_uptime(reduct_t* reduct)
{
    REDUCT_UNUSED(reduct);

    assert(reduct != NULL);

    return REDUCT_HANDLE_FROM_NUMBER((double)clock());
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

    if (reduct->global->argc == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    reduct_list_t* list = reduct_list_new(reduct);
    for (int i = 0; i < reduct->global->argc; i++)
    {
        reduct_list_push(reduct, list,
            REDUCT_HANDLE_FROM_ATOM(
                reduct_atom_new_copy(reduct, reduct->global->argv[i], strlen(reduct->global->argv[i]))));
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
            double cv = reduct_handle_as_number(reduct, current); \
            double av = reduct_handle_as_number(reduct, argv[i]); \
            current = REDUCT_HANDLE_FROM_NUMBER(cv _op av ? cv : av); \
        } \
        return current; \
    }

REDUCT_MATH_MIN_MAX_IMPL(reduct_min, <)
REDUCT_MATH_MIN_MAX_IMPL(reduct_max, >)

REDUCT_API reduct_handle_t reduct_clamp(reduct_t* reduct, reduct_handle_t val, reduct_handle_t minVal,
    reduct_handle_t maxVal)
{
    assert(reduct != NULL);

    double cv = reduct_handle_as_number(reduct, val);
    double mn = reduct_handle_as_number(reduct, minVal);
    double mx = reduct_handle_as_number(reduct, maxVal);
    if (cv < mn)
    {
        cv = mn;
    }
    if (cv > mx)
    {
        cv = mx;
    }

    return REDUCT_HANDLE_FROM_NUMBER(cv);
}

#define REDUCT_MATH_UNARY_IMPL(_name, _intFunc, _floatFunc) \
    REDUCT_API reduct_handle_t _name(reduct_t* reduct, reduct_handle_t val) \
    { \
        assert(reduct != NULL); \
        double f = reduct_handle_as_number(reduct, val); \
        return REDUCT_HANDLE_FROM_NUMBER((double)_floatFunc(f)); \
    }

#define REDUCT_INT_ABS(_x) ((_x) < 0 ? -(_x) : (_x))
REDUCT_MATH_UNARY_IMPL(reduct_abs, REDUCT_INT_ABS, fabs)
REDUCT_MATH_UNARY_IMPL(reduct_exp, exp, exp)
REDUCT_MATH_UNARY_IMPL(reduct_sqrt, sqrt, sqrt)

#define REDUCT_MATH_UNARY_TO_INT_IMPL(_name, _float_func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t val) \
    { \
        assert(reduct != NULL); \
        double f = reduct_handle_as_number(reduct, val); \
        return REDUCT_HANDLE_FROM_NUMBER((double)(int64_t)_float_func(f)); \
    }

REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_floor, floor)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_ceil, ceil)
REDUCT_MATH_UNARY_TO_INT_IMPL(reduct_round, round)

REDUCT_API reduct_handle_t reduct_pow(reduct_t* reduct, reduct_handle_t base, reduct_handle_t exp)
{
    assert(reduct != NULL);

    double bv = reduct_handle_as_number(reduct, base);
    double ev = reduct_handle_as_number(reduct, exp);
    return REDUCT_HANDLE_FROM_NUMBER(pow(bv, ev));
}

REDUCT_API reduct_handle_t reduct_log(struct reduct* reduct, reduct_handle_t val, reduct_handle_t base)
{
    assert(reduct != NULL);

    double vv = reduct_handle_as_number(reduct, val);
    if (REDUCT_HANDLE_IS_NIL(base))
    {
        return REDUCT_HANDLE_FROM_NUMBER(log(vv));
    }

    double bv = reduct_handle_as_number(reduct, base);
    return REDUCT_HANDLE_FROM_NUMBER(log(vv) / log(bv));
}

#define REDUCT_MATH_UNARY_FLOAT_IMPL(_name, _func) \
    REDUCT_API reduct_handle_t _name(struct reduct* reduct, reduct_handle_t val) \
    { \
        assert(reduct != NULL); \
        double f = reduct_handle_as_number(reduct, val); \
        return REDUCT_HANDLE_FROM_NUMBER((double)_func(f)); \
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

REDUCT_API reduct_handle_t reduct_atan2(struct reduct* reduct, reduct_handle_t y, reduct_handle_t x)
{
    assert(reduct != NULL);
    double yf = reduct_handle_as_number(reduct, y);
    double xf = reduct_handle_as_number(reduct, x);
    return REDUCT_HANDLE_FROM_NUMBER((double)atan2(yf, xf));
}

REDUCT_API reduct_handle_t reduct_rand(struct reduct* reduct, reduct_handle_t minVal, reduct_handle_t maxVal)
{
    assert(reduct != NULL);

    double mn = reduct_handle_as_number(reduct, minVal);
    double mx = reduct_handle_as_number(reduct, maxVal);
    double r = (double)rand() / (double)RAND_MAX;
    return REDUCT_HANDLE_FROM_NUMBER(mn + r * (mx - mn));
}

REDUCT_API reduct_handle_t reduct_seed(struct reduct* reduct, reduct_handle_t val)
{
    assert(reduct != NULL);
    int64_t i = reduct_handle_as_int(reduct, val);
    srand((unsigned int)i);
    return REDUCT_HANDLE_NIL(reduct);
}

#define REDUCT_STDLIB_WRAPPER_0(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc == 0, #_name ": expected 0 argument(s), got %zu", (size_t)argc); \
        (void)argv; \
        return _impl(reduct); \
    }

#define REDUCT_STDLIB_WRAPPER_1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc == 1, #_name ": expected 1 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, argv[0]); \
    }

#define REDUCT_STDLIB_WRAPPER_2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, argv[0], argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_3(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc == 3, #_name ": expected 3 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, argv[0], argv[1], argv[2]); \
    }

#define REDUCT_STDLIB_WRAPPER_R12(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 1 && argc <= 2, #_name ": expected 1 to 2 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, argv[0], argc == 2 ? argv[1] : REDUCT_HANDLE_NIL(reduct)); \
    }

#define REDUCT_STDLIB_WRAPPER_R23(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 2 && argc <= 3, #_name ": expected 2 to 3 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, argv[0], argv[1], argc == 3 ? argv[2] : REDUCT_HANDLE_NIL(reduct)); \
    }

#define REDUCT_STDLIB_WRAPPER_R34(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 3 && argc <= 4, #_name ": expected 3 to 4 argument(s), got %zu", \
            (size_t)argc); \
        return _impl(reduct, argv[0], argv[1], argv[2], argc == 4 ? argv[3] : REDUCT_HANDLE_NIL(reduct)); \
    }

#define REDUCT_STDLIB_WRAPPER_ARG2(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc == 2, #_name ": expected 2 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, argv[0], argv[1]); \
    }

#define REDUCT_STDLIB_WRAPPER_V1(_name, _impl) \
    static reduct_handle_t reduct_stdlib_##_name(reduct_t* reduct, size_t argc, reduct_handle_t* argv) \
    { \
        REDUCT_ERROR_ASSERT(reduct, argc >= 1, #_name ": expected at least 1 argument(s), got %zu", (size_t)argc); \
        return _impl(reduct, argc, argv); \
    }

REDUCT_STDLIB_WRAPPER_3(assert, reduct_assert)
REDUCT_STDLIB_WRAPPER_1(throw, reduct_throw)
REDUCT_STDLIB_WRAPPER_2(try, reduct_try)
REDUCT_STDLIB_WRAPPER_2(map, reduct_map)
REDUCT_STDLIB_WRAPPER_2(filter, reduct_filter)
REDUCT_STDLIB_WRAPPER_2(apply, reduct_apply)
REDUCT_STDLIB_WRAPPER_V1(is_atom, reduct_is_atom)
REDUCT_STDLIB_WRAPPER_V1(is_number, reduct_is_number)
REDUCT_STDLIB_WRAPPER_V1(is_lambda, reduct_is_lambda)
REDUCT_STDLIB_WRAPPER_V1(is_native, reduct_is_native)
REDUCT_STDLIB_WRAPPER_V1(is_callable, reduct_is_callable)
REDUCT_STDLIB_WRAPPER_V1(is_list, reduct_is_list)
REDUCT_STDLIB_WRAPPER_V1(is_empty, reduct_is_empty)
REDUCT_STDLIB_WRAPPER_V1(is_nil, reduct_is_nil)

static reduct_handle_t reduct_exact_equal_impl(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    if (argc < 2)
    {
        return REDUCT_HANDLE_TRUE();
    }
    for (size_t i = 0; i < argc - 1; i++)
    {
        if (!reduct_handle_is_equal(reduct, argv[i], argv[i + 1]))
        {
            return REDUCT_HANDLE_FALSE(reduct);
        }
    }
    return REDUCT_HANDLE_TRUE();
}

static reduct_handle_t reduct_exact_not_equal_impl(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    if (argc < 2)
    {
        return REDUCT_HANDLE_TRUE();
    }
    for (size_t i = 0; i < argc - 1; i++)
    {
        if (reduct_handle_is_equal(reduct, argv[i], argv[i + 1]))
        {
            return REDUCT_HANDLE_FALSE(reduct);
        }
    }
    return REDUCT_HANDLE_TRUE();
}

REDUCT_STDLIB_WRAPPER_V1(exact_eq, reduct_exact_equal_impl)
REDUCT_STDLIB_WRAPPER_V1(exact_neq, reduct_exact_not_equal_impl)

static reduct_handle_t reduct_stdlib_reduce(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc >= 2 && argc <= 3, "reduce: expected 2 to 3 argument(s), got %zu", (size_t)argc);
    return reduct_reduce(reduct, argv[0], argc == 3 ? argv[1] : REDUCT_HANDLE_NIL(reduct),
        argc == 3 ? argv[2] : argv[1]);
}

REDUCT_STDLIB_WRAPPER_R12(any, reduct_any)
REDUCT_STDLIB_WRAPPER_R12(all, reduct_all)
REDUCT_STDLIB_WRAPPER_R12(sort, reduct_sort)
REDUCT_STDLIB_WRAPPER_R12(flatten, reduct_flatten)
REDUCT_STDLIB_WRAPPER_R12(log, reduct_log)

static reduct_handle_t reduct_stdlib_concat(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    return reduct_concat(reduct, argc, argv);
}

static reduct_handle_t reduct_stdlib_append(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc >= 2, "append: expected at least 2 argument(s), got %zu", (size_t)argc);
    return reduct_append(reduct, argc, &argv[0]);
}

static reduct_handle_t reduct_stdlib_prepend(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc >= 2, "prepend: expected at least 2 argument(s), got %zu", (size_t)argc);
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
    REDUCT_ERROR_ASSERT(reduct, argc == 2, "repeat: expected 2 argument(s), got %zu", (size_t)argc);
    return reduct_repeat(reduct, argv[0], argv[1]);
}

REDUCT_STDLIB_WRAPPER_ARG2(starts_with, reduct_starts_with)
REDUCT_STDLIB_WRAPPER_ARG2(ends_with, reduct_ends_with)
REDUCT_STDLIB_WRAPPER_ARG2(join, reduct_join)
REDUCT_STDLIB_WRAPPER_ARG2(split, reduct_split)

REDUCT_STDLIB_WRAPPER_1(upper, reduct_upper)
REDUCT_STDLIB_WRAPPER_1(lower, reduct_lower)
REDUCT_STDLIB_WRAPPER_1(trim, reduct_trim)

static reduct_handle_t reduct_stdlib_number_impl(reduct_t* reduct, reduct_handle_t arg)
{
    return REDUCT_HANDLE_FROM_NUMBER(reduct_handle_as_number(reduct, arg));
}

REDUCT_STDLIB_WRAPPER_1(number, reduct_stdlib_number_impl)

REDUCT_STDLIB_WRAPPER_1(eval, reduct_eval)

static reduct_handle_t reduct_stdlib_parse_impl(reduct_t* reduct, reduct_handle_t arg)
{
    const char* str;
    size_t len;
    reduct_handle_atom_string(reduct, &arg, &str, &len);
    return reduct_parse(reduct, str, len, "<parse>");
}
REDUCT_STDLIB_WRAPPER_1(parse, reduct_stdlib_parse_impl)

REDUCT_STDLIB_WRAPPER_1(run, reduct_run)

static reduct_handle_t reduct_stdlib_import(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc >= 1 && argc <= 3, "import: expected 1 to 3 argument(s), got %zu", (size_t)argc);
    return reduct_import(reduct, argv[0], argc >= 2 ? argv[1] : REDUCT_HANDLE_NIL(reduct),
        argc == 3 ? argv[2] : REDUCT_HANDLE_NIL(reduct));
}

REDUCT_STDLIB_WRAPPER_1(read_file, reduct_read_file)
REDUCT_STDLIB_WRAPPER_3(write_file, reduct_write_file)
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

static reduct_handle_t reduct_stdlib_world(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc == 0, "world: expected 0 argument(s), got %zu", (size_t)argc);
    (void)argv;
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API void reduct_stdlib_register(reduct_t* reduct, reduct_stdlib_sets_t sets)
{
    assert(reduct != NULL);

    if (sets & REDUCT_STDLIB_STATE)
    {
        static reduct_native_t natives[] = {
            {"world", reduct_stdlib_world, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_ERROR)
    {
        static reduct_native_t natives[] = {
            {"assert!", reduct_stdlib_assert, NULL},
            {"throw", reduct_stdlib_throw, NULL},
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
            {"find", reduct_stdlib_find, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_SEQUENCES)
    {
        static reduct_native_t natives[] = {
            {"concat", reduct_stdlib_concat, NULL},
            {"append", reduct_stdlib_append, NULL},
            {"prepend", reduct_stdlib_prepend, NULL},
            {"first", reduct_stdlib_first, NULL},
            {"last", reduct_stdlib_last, NULL},
            {"rest", reduct_stdlib_rest, NULL},
            {"init", reduct_stdlib_init, NULL},
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
            {"number?", reduct_stdlib_is_number, NULL},
            {"lambda?", reduct_stdlib_is_lambda, NULL},
            {"native?", reduct_stdlib_is_native, NULL},
            {"callable?", reduct_stdlib_is_callable, NULL},
            {"list?", reduct_stdlib_is_list, NULL},
            {"empty?", reduct_stdlib_is_empty, NULL},
            {"nil?", reduct_stdlib_is_nil, NULL},
            {"exact==", reduct_stdlib_exact_eq, NULL},
            {"exact!=", reduct_stdlib_exact_neq, NULL},
        };
        reduct_native_register(reduct, natives, sizeof(natives) / sizeof(reduct_native_t));
    }
    if (sets & REDUCT_STDLIB_TYPE_CASTING)
    {
        static reduct_native_t natives[] = {
            {"number", reduct_stdlib_number, NULL},
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
