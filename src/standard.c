#include "reduct/list.h"
#include <reduct/atom.h>
#include <reduct/build.h>
#include <reduct/char.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/emit.h>
#include <reduct/error.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>
#include <reduct/native.h>
#include <reduct/parse.h>
#include <reduct/standard.h>
#include <reduct/stringify.h>
#include <reduct/task.h>

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
    reduct_error_t* error;
} reduct_standard_task_t;

static void reduct_standard_worker(reduct_t* reduct, void* arg)
{
    reduct_standard_task_t* task = (reduct_standard_task_t*)arg;
    reduct_error_t error = REDUCT_ERROR();

    REDUCT_ERROR_TRY(reduct, &error)
    {
        task->result = reduct_eval_call(reduct, task->callable, 1, &task->arg);
        REDUCT_HANDLE_RETAIN(reduct, task->result);
    }

    if (!REDUCT_ERROR_SUCCESS(&error) && REDUCT_ERROR_SUCCESS(task->error))
    {
        *task->error = error;
    }
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
        reduct_list_t* mapped = reduct_list_new(reduct, sourceList->length);
        reduct_list_retain(reduct, mapped);

        for (size_t i = 0; i < sourceList->length; i++)
        {
            mapped->handles[i] = reduct_eval_call(reduct, callable, 1, &sourceList->handles[i]);
        }

        reduct_list_release(reduct, mapped);
        return REDUCT_HANDLE_FROM_LIST(mapped);
    }

    reduct_error_t error = REDUCT_ERROR();

    reduct_handle_t mapped = REDUCT_HANDLE_CREATE_LIST(reduct, sourceList->length);
    reduct_list_t* mappedList = REDUCT_HANDLE_TO_LIST(mapped);
    REDUCT_HANDLE_RETAIN(reduct, mapped);
    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, sourceList->length);

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_standard_task_t* task = &tasks[i];
        task->error = &error;
        task->callable = callable;
        task->arg = sourceList->handles[i];
        if (!reduct_task_create(reduct, reduct_standard_worker, task, &task->id))
        {
            task->id = REDUCT_TASK_ID_INVALID;
            task->result = reduct_eval_call(reduct, callable, 1, &sourceList->handles[i]);
            REDUCT_HANDLE_RETAIN(reduct, task->result);
        }
    }

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
        mappedList->handles[i] = tasks[i].result;
        REDUCT_HANDLE_RELEASE(reduct, tasks[i].result);
    }

    REDUCT_SCRATCH_PUT(reduct, tasks);
    REDUCT_HANDLE_RELEASE(reduct, mapped);

    if (!REDUCT_ERROR_SUCCESS(&error))
    {
        REDUCT_ERROR_RETHROW(reduct, &error);
    }

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
        REDUCT_SCRATCH_GET(reduct, buf, reduct_handle_t, sourceList->length);
        size_t count = 0;

        for (size_t i = 0; i < sourceList->length; i++)
        {
            reduct_handle_t result = reduct_eval_call(reduct, callable, 1, &sourceList->handles[i]);
            if (REDUCT_HANDLE_IS_TRUTHY(result))
            {
                buf[count++] = sourceList->handles[i];
            }
        }

        reduct_list_t* filtered = reduct_list_new_handles(reduct, count, buf);
        REDUCT_SCRATCH_PUT(reduct, buf);

        return REDUCT_HANDLE_FROM_LIST(filtered);
    }

    reduct_error_t error = REDUCT_ERROR();

    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, sourceList->length);

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_standard_task_t* task = &tasks[i];
        task->error = &error;
        task->callable = callable;
        task->arg = sourceList->handles[i];
        if (!reduct_task_create(reduct, reduct_standard_worker, task, &task->id))
        {
            task->id = REDUCT_TASK_ID_INVALID;
            task->result = reduct_eval_call(reduct, callable, 1, &sourceList->handles[i]);
            REDUCT_HANDLE_RETAIN(reduct, task->result);
        }
    }

    for (size_t i = 0; i < sourceList->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
    }

    REDUCT_SCRATCH_GET(reduct, filteredHandles, reduct_handle_t, sourceList->length);
    size_t filteredCount = 0;

    for (size_t i = 0; i < sourceList->length; i++)
    {
        if (REDUCT_HANDLE_IS_TRUTHY(tasks[i].result))
        {
            filteredHandles[filteredCount++] = sourceList->handles[i];
        }
        REDUCT_HANDLE_RELEASE(reduct, tasks[i].result);
    }

    reduct_list_t* filtered = reduct_list_new_handles(reduct, filteredCount, filteredHandles);

    REDUCT_SCRATCH_PUT(reduct, filteredHandles);
    REDUCT_SCRATCH_PUT(reduct, tasks);

    if (!REDUCT_ERROR_SUCCESS(&error))
    {
        REDUCT_ERROR_RETHROW(reduct, &error);
    }

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

    for (size_t i = 0; i < sourceList->length; i++)
    {
        if (REDUCT_HANDLE_IS_NIL(accumulator))
        {
            accumulator = sourceList->handles[i];
            REDUCT_HANDLE_RETAIN(reduct, accumulator);
            continue;
        }

        reduct_handle_t args[2] = {accumulator, sourceList->handles[i]};
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
    if (sourceList->length == 0)
    {
        return reduct_eval_call(reduct, callable, 0, NULL);
    }

    return reduct_eval_call(reduct, callable, sourceList->length, sourceList->handles);
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
        for (size_t i = 0; i < list->length; i++) \
        { \
            reduct_handle_t result = reduct_eval_maybe_call(reduct, fn, list->handles[i]); \
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

    memcpy(a, listVal->handles, len * sizeof(reduct_handle_t));

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

    reduct_list_t* resultList = reduct_list_new_handles(reduct, len, src);

    REDUCT_SCRATCH_PUT(reduct, a);
    REDUCT_SCRATCH_PUT(reduct, b);
    return REDUCT_HANDLE_FROM_LIST(resultList);
}

static inline uint64_t reduct_handle_normalize_index(reduct_t* reduct, reduct_handle_t index, size_t length)
{
    if (REDUCT_LIKELY(REDUCT_HANDLE_IS_NUMBER(index)))
    {
        double d = REDUCT_HANDLE_TO_NUMBER(index);
        if (REDUCT_LIKELY(d >= 0))
        {
            return (uint64_t)d;
        }
        int64_t n = (int64_t)d + (int64_t)length;
        REDUCT_ERROR_ASSERT(reduct, n >= 0, "index out of range");
        return (uint64_t)n;
    }

    return (uint64_t)reduct_handle_as_int(reduct, index);
}

static inline void reduct_sequence_normalize_range(reduct_t* reduct, reduct_handle_t startHandle,
    reduct_handle_t endHandle, size_t length, size_t* outStart, size_t* outEnd)
{
    size_t start = reduct_handle_normalize_index(reduct, startHandle, length);
    size_t end;

    if (!REDUCT_HANDLE_IS_NIL(endHandle))
    {
        end = reduct_handle_normalize_index(reduct, endHandle, length);
    }
    else
    {
        end = length;
    }

    start = REDUCT_MAX(0, REDUCT_MIN(start, (size_t)length));
    end = REDUCT_MAX(0, REDUCT_MIN(end, (size_t)length));

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

    reduct_list_t* list = reduct_list_new(reduct, count);

    int64_t current = startVal;
    for (size_t i = 0; i < count; i++)
    {
        list->handles[i] = REDUCT_HANDLE_FROM_NUMBER((double)current);
        current += stepVal;
    }

    return REDUCT_HANDLE_FROM_LIST(list);
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
        if (argc == 0)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }

        if (argc == 1)
        {
            return argv[0];
        }

        if (argc == 2)
        {
            if (REDUCT_HANDLE_IS_LIST(argv[0]) && REDUCT_HANDLE_IS_LIST(argv[1]))
            {
                return REDUCT_HANDLE_FROM_LIST(
                    reduct_list_concat(reduct, REDUCT_HANDLE_TO_LIST(argv[0]), REDUCT_HANDLE_TO_LIST(argv[1])));
            }

            if (REDUCT_HANDLE_IS_LIST(argv[0]))
            {
                return REDUCT_HANDLE_FROM_LIST(reduct_list_append(reduct, REDUCT_HANDLE_TO_LIST(argv[0]), argv[1]));
            }

            if (REDUCT_HANDLE_IS_LIST(argv[1]))
            {
                return REDUCT_HANDLE_FROM_LIST(reduct_list_prepend(reduct, REDUCT_HANDLE_TO_LIST(argv[1]), argv[0]));
            }
        }

        size_t totalCount = 0;
        for (size_t i = 0; i < argc; i++)
        {
            totalCount += reduct_handle_len(reduct, argv[i]);
        }

        reduct_list_t* newList = reduct_list_new(reduct, totalCount);
        size_t offset = 0;
        for (size_t i = 0; i < argc; i++)
        {
            if (REDUCT_HANDLE_IS_LIST(argv[i]))
            {
                reduct_list_t* src = REDUCT_HANDLE_TO_LIST(argv[i]);
                if (src->length > 0)
                {
                    memcpy(newList->handles + offset, src->handles, src->length * sizeof(reduct_handle_t));
                    offset += src->length;
                }
            }
            else
            {
                newList->handles[offset++] = argv[i];
            }
        }

        return REDUCT_HANDLE_FROM_LIST(newList);
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

    if (argc == 2)
    {
        return REDUCT_HANDLE_FROM_LIST(reduct_list_append(reduct, REDUCT_HANDLE_TO_LIST(argv[0]), argv[1]));
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(argv[0]);

    size_t extra = argc - 1;
    reduct_list_t* newList = reduct_list_new(reduct, list->length + extra);

    if (list->length > 0)
    {
        memcpy(newList->handles, list->handles, list->length * sizeof(reduct_handle_t));
    }

    for (size_t i = 0; i < extra; i++)
    {
        newList->handles[list->length + i] = argv[i + 1];
    }

    return REDUCT_HANDLE_FROM_LIST(newList);
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

    if (argc == 2)
    {
        return REDUCT_HANDLE_FROM_LIST(reduct_list_prepend(reduct, REDUCT_HANDLE_TO_LIST(argv[0]), argv[1]));
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(argv[0]);
    size_t extra = argc - 1;
    reduct_list_t* newList = reduct_list_new(reduct, list->length + extra);

    for (size_t i = 0; i < extra; i++)
    {
        newList->handles[i] = argv[i + 1];
    }

    if (list->length > 0)
    {
        memcpy(newList->handles + extra, list->handles, list->length * sizeof(reduct_handle_t));
    }

    return REDUCT_HANDLE_FROM_LIST(newList);
}

static inline reduct_handle_t reduct_sequence_edge(reduct_t* reduct, reduct_handle_t handle, bool first)
{
    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        if (list->length == 0)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }

        size_t index = first ? 0 : list->length - 1;
        return list->handles[index];
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        if (atom->length == 0)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }

        size_t index = first ? 0 : atom->length - 1;
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, atom, index, 1));
    }

    return REDUCT_HANDLE_NIL(reduct);
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

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        size_t n = reduct_handle_normalize_index(reduct, index, list->length);
        if (n >= list->length)
        {
            return defaultVal;
        }

        return list->handles[n];
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        size_t n = reduct_handle_normalize_index(reduct, index, atom->length);
        if (n >= atom->length)
        {
            return defaultVal;
        }

        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, atom, n, 1));
    }

    REDUCT_ERROR_THROW(reduct, "nth: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_assoc(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t value, reduct_handle_t fillVal)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        size_t n = reduct_handle_normalize_index(reduct, index, list->length);

        size_t newLen = REDUCT_MAX(list->length, n + 1);
        reduct_list_t* newList = reduct_list_new(reduct, newLen);
        if (list->length > 0)
        {
            memcpy(newList->handles, list->handles, list->length * sizeof(reduct_handle_t));
        }

        for (size_t i = list->length; i < n; i++)
        {
            newList->handles[i] = fillVal;
        }

        newList->handles[n] = value;
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        size_t n = reduct_handle_normalize_index(reduct, index, atom->length);

        if (n > atom->length && REDUCT_HANDLE_IS_NIL(fillVal))
        {
            REDUCT_ERROR_THROW(reduct, "assoc: index %zu out of range for atom and no fill value provided", n);
        }

        reduct_atom_t* val = reduct_handle_as_atom(reduct, value);
        reduct_atom_t* fill =
            (n > atom->length && !REDUCT_HANDLE_IS_NIL(fillVal)) ? reduct_handle_as_atom(reduct, fillVal) : NULL;

        size_t prefixLen = REDUCT_MIN(atom->length, n);
        size_t fillCount = (n > atom->length) ? n - atom->length : 0;
        size_t suffixStart = n + 1;
        size_t suffixLen = (suffixStart < atom->length) ? atom->length - suffixStart : 0;

        size_t resultLen = prefixLen + val->length + suffixLen;
        if (fillCount > 0 && fill)
        {
            resultLen += fillCount * fill->length;
        }

        reduct_atom_t* result = reduct_atom_new(reduct, resultLen);
        char* dst = result->string;

        if (prefixLen > 0)
        {
            memcpy(dst, atom->string, prefixLen);
            dst += prefixLen;
        }

        for (size_t i = 0; i < fillCount && fill; i++)
        {
            memcpy(dst, fill->string, fill->length);
            dst += fill->length;
        }

        memcpy(dst, val->string, val->length);
        dst += val->length;

        if (suffixLen > 0)
        {
            memcpy(dst, atom->string + suffixStart, suffixLen);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }

    REDUCT_ERROR_THROW(reduct, "assoc: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_dissoc(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        size_t n = reduct_handle_normalize_index(reduct, index, list->length);
        if (n >= list->length)
        {
            return handle;
        }

        reduct_list_t* newList = reduct_list_new(reduct, list->length - 1);
        if (n > 0)
        {
            memcpy(newList->handles, list->handles, n * sizeof(reduct_handle_t));
        }
        if (n + 1 < list->length)
        {
            memcpy(newList->handles + n, list->handles + n + 1, (list->length - n - 1) * sizeof(reduct_handle_t));
        }
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        size_t n = reduct_handle_normalize_index(reduct, index, atom->length);
        if (n >= atom->length)
        {
            return handle;
        }

        reduct_atom_t* result = reduct_atom_new(reduct, atom->length - 1);
        char* dst = result->string;

        if (n > 0)
        {
            memcpy(dst, atom->string, n);
            dst += n;
        }
        if (n + 1 < atom->length)
        {
            memcpy(dst, atom->string + n + 1, atom->length - n - 1);
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }

    REDUCT_ERROR_THROW(reduct, "dissoc: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_update(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t callable, reduct_handle_t fillVal)
{
    assert(reduct != NULL);

    reduct_handle_t currentVal = reduct_nth(reduct, handle, index, fillVal);
    reduct_handle_t newVal = reduct_eval_call(reduct, callable, 1, &currentVal);
    return reduct_assoc(reduct, handle, index, newVal, fillVal);
}

REDUCT_API reduct_handle_t reduct_index_of(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t target)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        for (size_t i = 0; i < list->length; i++)
        {
            if (reduct_handle_compare(reduct, list->handles[i], target) == 0)
            {
                return REDUCT_HANDLE_FROM_NUMBER((double)i);
            }
        }
        return REDUCT_HANDLE_NIL(reduct);
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        const char* srcStr;
        size_t srcLen;
        reduct_handle_atom_string(reduct, &handle, &srcStr, &srcLen);

        const char* targetStr;
        size_t targetLen;
        reduct_handle_atom_string(reduct, &target, &targetStr, &targetLen);

        if (targetLen == 0)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }

        if (targetLen <= srcLen)
        {
            for (size_t i = 0; i <= srcLen - targetLen; i++)
            {
                if (memcmp(srcStr + i, targetStr, targetLen) == 0)
                {
                    return REDUCT_HANDLE_FROM_NUMBER((double)i);
                }
            }
        }
        return REDUCT_HANDLE_NIL(reduct);
    }

    REDUCT_ERROR_THROW(reduct, "index-of: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_reverse(reduct_t* reduct, reduct_handle_t handle)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        size_t len = list->length;
        reduct_list_t* newList = reduct_list_new(reduct, len);
        for (size_t i = 0; i < len; i++)
        {
            newList->handles[i] = list->handles[len - 1 - i];
        }
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        size_t len = atom->length;
        if (len <= 1)
        {
            return handle;
        }

        reduct_atom_t* result = reduct_atom_new(reduct, len);
        const char* src = atom->string;
        char* dst = result->string;

        for (size_t i = 0; i < len; i++)
        {
            dst[i] = src[len - 1 - i];
        }

        return REDUCT_HANDLE_FROM_ATOM(result);
    }

    REDUCT_ERROR_THROW(reduct, "reverse: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

REDUCT_API reduct_handle_t reduct_slice(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t start,
    reduct_handle_t end)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
        size_t startVal;
        size_t endVal;
        reduct_sequence_normalize_range(reduct, start, end, list->length, &startVal, &endVal);
        if (startVal >= endVal)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }
        return REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, list, startVal, endVal));
    }

    if (REDUCT_HANDLE_IS_ATOM_LIKE(handle))
    {
        reduct_atom_t* atom = reduct_handle_as_atom(reduct, handle);
        size_t startVal;
        size_t endVal;
        reduct_sequence_normalize_range(reduct, start, end, atom->length, &startVal, &endVal);
        if (startVal >= endVal)
        {
            return REDUCT_HANDLE_NIL(reduct);
        }
        return REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, atom, startVal, endVal - startVal));
    }

    REDUCT_ERROR_THROW(reduct, "slice: expected list or atom, got %s", REDUCT_HANDLE_GET_TYPE_STRING(handle));
}

static void reduct_flatten_impl(reduct_t* reduct, reduct_handle_t handle, int64_t depth, reduct_handle_t** buf,
    size_t* count, size_t* capacity)
{
    if (depth == 0 || !REDUCT_HANDLE_IS_LIST(handle))
    {
        if (*count >= *capacity)
        {
            *capacity = (*capacity == 0) ? 16 : (*capacity * 2);
            REDUCT_SCRATCH_GROW(reduct, *buf, reduct_handle_t, *capacity);
        }
        (*buf)[(*count)++] = handle;
        return;
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    for (size_t i = 0; i < list->length; i++)
    {
        reduct_flatten_impl(reduct, list->handles[i], depth - 1, buf, count, capacity);
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

    size_t count = 0;
    size_t capacity = 16;
    REDUCT_SCRATCH_GET(reduct, buf, reduct_handle_t, capacity);

    reduct_flatten_impl(reduct, handle, depthVal, &buf, &count, &capacity);

    reduct_list_t* result = reduct_list_new_handles(reduct, count, buf);
    REDUCT_SCRATCH_PUT(reduct, buf);

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
        reduct_list_t* result = reduct_list_new(reduct, list->length);

        for (size_t i = 0; i < list->length; i++)
        {
            result->handles[i] =
                (reduct_handle_compare(reduct, list->handles[i], oldVal) == 0) ? newVal : list->handles[i];
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

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    REDUCT_SCRATCH_GET(reduct, buf, reduct_handle_t, list->length);

    size_t count = 0;
    for (size_t i = 0; i < list->length; i++)
    {
        reduct_handle_t current = list->handles[i];
        bool found = false;
        for (size_t j = 0; j < count; j++)
        {
            if (reduct_handle_compare(reduct, current, buf[j]) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            buf[count++] = current;
        }
    }

    reduct_handle_t result = REDUCT_HANDLE_CREATE_HANDLES(reduct, count, buf);
    REDUCT_SCRATCH_PUT(reduct, buf);
    return result;
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

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(handle);
    size_t chunkSize = (size_t)n;
    size_t chunkCount = (list->length + chunkSize - 1) / chunkSize;

    reduct_list_t* result = reduct_list_new(reduct, chunkCount);
    for (size_t i = 0; i < list->length; i += chunkSize)
    {
        size_t end = REDUCT_MIN(i + chunkSize, list->length);
        reduct_list_t* chunk = reduct_list_slice(reduct, list, i, end);
        result->handles[i / chunkSize] = REDUCT_HANDLE_FROM_LIST(chunk);
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
        for (size_t i = 0; i < list->length; i++)
        {
            reduct_handle_t current = list->handles[i];
            reduct_handle_t result = reduct_eval_call(reduct, callable, 1, &current);
            if (REDUCT_HANDLE_IS_TRUTHY(result))
            {
                return current;
            }
        }

        return REDUCT_HANDLE_NIL(reduct);
    }

    reduct_error_t error = REDUCT_ERROR();

    REDUCT_SCRATCH_GET(reduct, tasks, reduct_standard_task_t, list->length);

    for (size_t i = 0; i < list->length; i++)
    {
        reduct_standard_task_t* task = &tasks[i];
        task->error = &error;
        task->callable = callable;
        task->arg = list->handles[i];        
        if (!reduct_task_create(reduct, reduct_standard_worker, task, &task->id))
        {
            task->id = REDUCT_TASK_ID_INVALID;
            task->result = reduct_eval_call(reduct, callable, 1, &list->handles[i]);
            REDUCT_HANDLE_RETAIN(reduct, task->result);
        }
    }

    for (size_t i = 0; i < list->length; i++)
    {
        reduct_task_join(reduct, tasks[i].id);
    }

    if (!REDUCT_ERROR_SUCCESS(&error))
    {
        REDUCT_SCRATCH_PUT(reduct, tasks);
        REDUCT_ERROR_RETHROW(reduct, &error);
    }

    for (size_t i = 0; i < list->length; i++)
    {
        reduct_standard_task_t* task = &tasks[i];
        if (REDUCT_HANDLE_IS_TRUTHY(task->result))
        {
            REDUCT_SCRATCH_PUT(reduct, tasks);
            return task->result;
        }
    }

    REDUCT_SCRATCH_PUT(reduct, tasks);
    return REDUCT_HANDLE_NIL(reduct);
}

REDUCT_API reduct_handle_t reduct_list_find_entry(reduct_t* reduct, reduct_list_t* list, reduct_handle_t key)
{
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(key), "key must be an atom");

    reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));
    for (size_t i = 0; i < list->length; i++)
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(list->handles[i])))
        {
            continue;
        }

        reduct_list_t* entry = REDUCT_HANDLE_TO_LIST(list->handles[i]);
        if (REDUCT_UNLIKELY(entry->length < 1))
        {
            continue;
        }

        reduct_handle_t entryKey = entry->handles[0];
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ATOM(entryKey)))
        {
            continue;
        }

        reduct_atom_t* entryKeyInterned = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(entryKey));
        if (internedKey == entryKeyInterned)
        {
            return list->handles[i];
        }
    }

    return REDUCT_HANDLE_NIL(reduct);
}

static inline reduct_list_t* reduct_find_pair(reduct_t* reduct, reduct_list_t* list, reduct_handle_t key)
{
    REDUCT_ERROR_ASSERT(reduct, REDUCT_HANDLE_IS_ATOM(key), "key must be an atom");

    reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));
    for (size_t i = 0; i < list->length; i++)
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(list->handles[i])))
        {
            continue;
        }

        reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(list->handles[i]);
        if (REDUCT_UNLIKELY(pair->length < 2))
        {
            continue;
        }

        reduct_handle_t pairKey = pair->handles[0];
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_ATOM(pairKey)))
        {
            continue;
        }

        reduct_atom_t* entryKeyInterned = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(pairKey));
        if (internedKey == entryKeyInterned)
        {
            return pair;
        }
    }

    return NULL;
}

static reduct_handle_t reduct_alist_assoc(reduct_t* reduct, reduct_handle_t listHandle, reduct_handle_t key,
    reduct_handle_t val)
{
    if (!REDUCT_HANDLE_IS_LIST(listHandle))
    {
        reduct_list_t* pair = reduct_list_new(reduct, 2);
        pair->handles[0] = key;
        pair->handles[1] = val;
        reduct_list_t* newList = reduct_list_new(reduct, 1);
        newList->handles[0] = REDUCT_HANDLE_FROM_LIST(pair);
        return REDUCT_HANDLE_FROM_LIST(newList);
    }

    reduct_list_t* list = REDUCT_HANDLE_TO_LIST(listHandle);
    reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));

    for (uint32_t i = 0; i < list->length; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(list->handles[i]))
        {
            continue;
        }
        reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(list->handles[i]);
        if (pair->length < 1 || !REDUCT_HANDLE_IS_ATOM(pair->handles[0]))
        {
            continue;
        }

        if (reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(pair->handles[0])) == internedKey)
        {
            if (pair->length >= 2 && pair->handles[1]._value == val._value)
            {
                return listHandle;
            }

            reduct_list_t* newList = reduct_list_new_handles(reduct, list->length, list->handles);
            reduct_list_t* newPair = reduct_list_new(reduct, 2);
            newPair->handles[0] = key;
            newPair->handles[1] = val;
            newList->handles[i] = REDUCT_HANDLE_FROM_LIST(newPair);
            return REDUCT_HANDLE_FROM_LIST(newList);
        }
    }

    reduct_list_t* newPair = reduct_list_new(reduct, 2);
    newPair->handles[0] = key;
    newPair->handles[1] = val;
    return REDUCT_HANDLE_FROM_LIST(reduct_list_append(reduct, list, REDUCT_HANDLE_FROM_LIST(newPair)));
}

REDUCT_API reduct_handle_t reduct_get_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t defaultVal)
{
    reduct_handle_t current = list;
    if (REDUCT_LIKELY(!REDUCT_HANDLE_IS_LIST(path)))
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(current)))
        {
            return defaultVal;
        }

        reduct_list_t* pair = reduct_find_pair(reduct, REDUCT_HANDLE_TO_LIST(current), path);
        if (REDUCT_UNLIKELY(pair == NULL))
        {
            return defaultVal;
        }

        return pair->handles[1];
    }

    reduct_list_t* pathList = REDUCT_HANDLE_TO_LIST(path);
    for (size_t i = 0; i < pathList->length; i++)
    {
        if (REDUCT_UNLIKELY(!REDUCT_HANDLE_IS_LIST(current)))
        {
            return defaultVal;
        }

        reduct_list_t* pair = reduct_find_pair(reduct, REDUCT_HANDLE_TO_LIST(current), pathList->handles[i]);
        if (REDUCT_UNLIKELY(pair == NULL))
        {
            return defaultVal;
        }

        current = pair->handles[1];
    }

    return current;
}

REDUCT_API reduct_handle_t reduct_assoc_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t val)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_NIL(path))
    {
        return val;
    }

    reduct_handle_t key;
    reduct_handle_t rest;

    if (REDUCT_HANDLE_IS_LIST(path))
    {
        reduct_list_t* p = REDUCT_HANDLE_TO_LIST(path);
        if (p->length == 0)
        {
            return val;
        }
        key = p->handles[0];
        rest = (p->length > 1) ? REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, p, 1, p->length))
                               : REDUCT_HANDLE_NIL(reduct);
    }
    else
    {
        key = path;
        rest = REDUCT_HANDLE_NIL(reduct);
    }

    reduct_handle_t sub = REDUCT_HANDLE_NIL(reduct);
    if (REDUCT_HANDLE_IS_LIST(list))
    {
        reduct_list_t* pair = reduct_find_pair(reduct, REDUCT_HANDLE_TO_LIST(list), key);
        if (pair != NULL)
        {
            sub = pair->handles[1];
        }
    }

    reduct_handle_t newVal = reduct_assoc_in(reduct, sub, rest, val);
    return reduct_alist_assoc(reduct, list, key, newVal);
}

REDUCT_API reduct_handle_t reduct_dissoc_in(reduct_t* reduct, reduct_handle_t list, reduct_handle_t path)
{
    assert(reduct != NULL);

    if (REDUCT_HANDLE_IS_NIL(path) || !REDUCT_HANDLE_IS_LIST(list))
    {
        return list;
    }

    reduct_handle_t key;
    reduct_handle_t rest;

    if (REDUCT_HANDLE_IS_LIST(path))
    {
        reduct_list_t* p = REDUCT_HANDLE_TO_LIST(path);
        if (p->length == 0)
        {
            return list;
        }
        key = p->handles[0];
        rest = (p->length > 1) ? REDUCT_HANDLE_FROM_LIST(reduct_list_slice(reduct, p, 1, p->length))
                               : REDUCT_HANDLE_NIL(reduct);
    }
    else
    {
        key = path;
        rest = REDUCT_HANDLE_NIL(reduct);
    }

    reduct_list_t* l = REDUCT_HANDLE_TO_LIST(list);

    if (REDUCT_HANDLE_IS_NIL(rest))
    {
        reduct_atom_t* internedKey = reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(key));
        for (uint32_t i = 0; i < l->length; i++)
        {
            if (!REDUCT_HANDLE_IS_LIST(l->handles[i]))
            {
                continue;
            }
            reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(l->handles[i]);
            if (pair->length < 1 || !REDUCT_HANDLE_IS_ATOM(pair->handles[0]))
            {
                continue;
            }

            if (reduct_atom_ensure_interned(reduct, REDUCT_HANDLE_TO_ATOM(pair->handles[0])) == internedKey)
            {
                reduct_list_t* newList = reduct_list_new(reduct, l->length - 1);
                if (i > 0)
                {
                    memcpy(newList->handles, l->handles, i * sizeof(reduct_handle_t));
                }
                if (i + 1 < l->length)
                {
                    memcpy(newList->handles + i, l->handles + i + 1, (l->length - i - 1) * sizeof(reduct_handle_t));
                }
                return REDUCT_HANDLE_FROM_LIST(newList);
            }
        }
        return list;
    }
    else
    {
        reduct_list_t* pair = reduct_find_pair(reduct, l, key);
        if (pair == NULL)
        {
            return list;
        }

        reduct_handle_t newVal = reduct_dissoc_in(reduct, pair->handles[1], rest);
        if (newVal._value == pair->handles[1]._value)
        {
            return list;
        }
        return reduct_alist_assoc(reduct, list, key, newVal);
    }
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

    reduct_list_t* src = REDUCT_HANDLE_TO_LIST(list);
    REDUCT_SCRATCH_GET(reduct, buf, reduct_handle_t, src->length);
    size_t count = 0;

    for (size_t i = 0; i < src->length; i++)
    {
        reduct_handle_t entry = src->handles[i];
        if (REDUCT_HANDLE_IS_LIST(entry))
        {
            reduct_list_t* entryList = REDUCT_HANDLE_TO_LIST(entry);
            if (entryList->length > index)
            {
                buf[count++] = entryList->handles[index];
            }
        }
    }

    reduct_list_t* resultList = reduct_list_new_handles(reduct, count, buf);
    REDUCT_SCRATCH_PUT(reduct, buf);
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

    size_t worstLength = 0;
    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            continue;
        }
        worstLength += REDUCT_HANDLE_TO_LIST(argv[i])->length;
    }

    REDUCT_SCRATCH_GET(reduct, buf, reduct_handle_t, worstLength);
    size_t count = 0;

    for (size_t i = 0; i < argc; i++)
    {
        if (!REDUCT_HANDLE_IS_LIST(argv[i]))
        {
            continue;
        }

        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(argv[i]);
        for (size_t j = 0; j < list->length; j++)
        {
            if (!REDUCT_HANDLE_IS_LIST(list->handles[j]))
            {
                continue;
            }

            reduct_list_t* pair = REDUCT_HANDLE_TO_LIST(list->handles[j]);
            if (pair->length < 2)
            {
                continue;
            }

            bool found = false;
            for (size_t k = 0; k < count; k++)
            {
                reduct_list_t* existingPair = REDUCT_HANDLE_TO_LIST(buf[k]);
                if (reduct_handle_is_equal(reduct, existingPair->handles[0], pair->handles[0]))
                {
                    buf[k] = list->handles[j];
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                buf[count++] = list->handles[j];
            }
        }
    }

    reduct_handle_t result = REDUCT_HANDLE_CREATE_HANDLES(reduct, count, buf);
    REDUCT_SCRATCH_PUT(reduct, buf);
    return result;
}

REDUCT_API reduct_handle_t reduct_explode(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(argv != NULL || argc == 0);

    size_t totalCount = 0;
    for (size_t i = 0; i < argc; i++)
    {
        totalCount += reduct_handle_len(reduct, argv[i]);
    }

    reduct_list_t* list = reduct_list_new(reduct, totalCount);
    size_t offset = 0;
    for (size_t i = 0; i < argc; i++)
    {
        const char* str;
        size_t len;
        reduct_handle_atom_string(reduct, &argv[i], &str, &len);
        for (size_t j = 0; j < len; j++)
        {
            list->handles[offset++] = REDUCT_HANDLE_FROM_NUMBER((double)(int64_t)(unsigned char)str[j]);
        }
    }

    return REDUCT_HANDLE_FROM_LIST(list);
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

        reduct_list_t* list = REDUCT_HANDLE_TO_LIST(argv[i]);
        for (size_t j = 0; j < list->length; j++)
        {
            *dest++ = (char)(uint8_t)reduct_handle_as_int(reduct, list->handles[j]);
        }
    }

    return REDUCT_HANDLE_FROM_ATOM(result);
}

REDUCT_API reduct_handle_t reduct_repeat(reduct_t* reduct, reduct_handle_t handle, reduct_handle_t count)
{
    assert(reduct != NULL);

    int64_t n = reduct_handle_as_int(reduct, count);
    REDUCT_ERROR_ASSERT(reduct, n >= 0, "repeat: count must be non-negative, got %lld", n);

    size_t len = (size_t)n;
    reduct_list_t* newList = reduct_list_new(reduct, len);
    for (size_t i = 0; i < len; i++)
    {
        newList->handles[i] = handle;
    }

    return REDUCT_HANDLE_FROM_LIST(newList);
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
        reduct_handle_t edge = list->handles[index];
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

    const char* sepStr;
    size_t sepLen;
    reduct_handle_atom_string(reduct, &sepHandle, &sepStr, &sepLen);

    size_t totalLen = 0;
    for (size_t i = 0; i < list->length; i++)
    {
        totalLen += reduct_handle_len(reduct, list->handles[i]);
        if (i + 1 < list->length)
        {
            totalLen += sepLen;
        }
    }

    reduct_atom_t* result = reduct_atom_new(reduct, totalLen);
    char* dst = result->string;

    for (size_t i = 0; i < list->length; i++)
    {
        const char* srcStr;
        size_t srcLen;
        reduct_handle_atom_string(reduct, &list->handles[i], &srcStr, &srcLen);

        if (srcLen > 0)
        {
            memcpy(dst, srcStr, srcLen);
            dst += srcLen;
        }

        if (i + 1 < list->length && sepLen > 0)
        {
            memcpy(dst, sepStr, sepLen);
            dst += sepLen;
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

    if (sepLen == 0)
    {
        reduct_list_t* resultList = reduct_list_new(reduct, srcLen);
        for (size_t i = 0; i < srcLen; i++)
        {
            resultList->handles[i] =
                REDUCT_HANDLE_FROM_ATOM(reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), i, 1));
        }

        return REDUCT_HANDLE_FROM_LIST(resultList);
    }

    REDUCT_SCRATCH_GET(reduct, parts, reduct_handle_t, 16);
    size_t count = 0;
    size_t capacity = 16;

    size_t lastPos = 0;
    for (size_t i = 0; i <= srcLen - sepLen; i++)
    {
        if (memcmp(srcStr + i, sepStr, sepLen) == 0)
        {
            if (count >= capacity)
            {
                capacity *= 2;
                REDUCT_SCRATCH_GROW(reduct, parts, reduct_handle_t, capacity);
            }
            parts[count++] = REDUCT_HANDLE_FROM_ATOM(
                reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, i - lastPos));
            i += sepLen - 1;
            lastPos = i + 1;
        }
    }

    if (count >= capacity)
    {
        REDUCT_SCRATCH_GROW(reduct, parts, reduct_handle_t, count + 1);
    }
    parts[count++] = REDUCT_HANDLE_FROM_ATOM(
        reduct_atom_substr(reduct, REDUCT_HANDLE_TO_ATOM(srcHandle), lastPos, srcLen - lastPos));

    reduct_list_t* resultList = reduct_list_new_handles(reduct, count, parts);
    REDUCT_SCRATCH_PUT(reduct, parts);

    return REDUCT_HANDLE_FROM_LIST(resultList);
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

    reduct_list_t* list = reduct_list_new(reduct, count);
    for (size_t i = 0; i < count; i++)
    {
        char* env = environ[i];
        char* eq = strchr(env, '=');
        if (eq != NULL)
        {
            reduct_list_t* pair = reduct_list_new(reduct, 2);
            pair->handles[0] = REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, env, (size_t)(eq - env)));
            pair->handles[1] = REDUCT_HANDLE_FROM_ATOM(reduct_atom_new_copy(reduct, eq + 1, strlen(eq + 1)));

            list->handles[i] = REDUCT_HANDLE_FROM_LIST(pair);
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

    reduct_list_t* list = reduct_list_new(reduct, reduct->global->argc);
    for (int i = 0; i < reduct->global->argc; i++)
    {
        list->handles[i] = REDUCT_HANDLE_FROM_ATOM(
            reduct_atom_new_copy(reduct, reduct->global->argv[i], strlen(reduct->global->argv[i])));
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

static reduct_handle_t reduct_stdlib_nth(reduct_t* reduct, size_t argc, reduct_handle_t* argv)
{
    REDUCT_ERROR_ASSERT(reduct, argc != 2 && argc != 3, "nth: expected 2 or 3 argument(s), got %zu", (size_t)argc);
    return reduct_nth(reduct, argv[0], argv[1], argc == 3 ? argv[2] : REDUCT_HANDLE_NIL(reduct));
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
            {"nth", reduct_stdlib_nth, NULL},
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
