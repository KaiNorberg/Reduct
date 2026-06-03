#include "reduct/task.h"
#include <reduct/future.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/list.h>
#include <reduct/handle.h>
#include <reduct/gc.h>

static void reduct_future_worker(reduct_t* reduct, void* arg)
{
    reduct_item_t* item = (reduct_item_t*)arg;
    reduct_future_t* future = &item->future;

    reduct_error_t err = REDUCT_ERROR();
    REDUCT_ERROR_TRY(reduct, &err)
    {
        future->result = reduct_eval_call(reduct, future->callable, future->argc, future->argv);
    }
    else
    {
        future->result = REDUCT_HANDLE_NIL(reduct);
        future->error = malloc(sizeof(reduct_error_t));        
        if (future->error != NULL)
        {
            memcpy(future->error, &err, sizeof(reduct_error_t));
        }
        else
        {
            REDUCT_ERROR_THROW(reduct, "out of memory");
        }
    }

    atomic_store_explicit(&future->done, true, memory_order_release);
}

REDUCT_API reduct_future_t* reduct_future_new(struct reduct* reduct, reduct_handle_t callable, size_t argc, reduct_handle_t* argv)
{
    assert(reduct != NULL);
    assert(REDUCT_HANDLE_IS_CALLABLE(reduct, callable));
    assert(argc == 0 || argv != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_FUTURE;

    reduct_future_t* future = &item->future;
    future->error = NULL;
    future->callable = callable;
    if (argc >= REDUCT_FUTURE_SMALL_MAX)
    {
        future->argv = malloc(argc * sizeof(reduct_handle_t));
        if (future->argv == NULL)
        {
            REDUCT_ERROR_THROW(reduct, "out of memory");
        }
    }
    else
    {
        future->argv = future->smallArgv;
    }
    memcpy(future->argv, argv, argc * sizeof(reduct_handle_t));
    future->argc = argc;
    atomic_init(&future->done, false);

    if (!reduct_task_create_try(reduct, reduct_future_worker, item, &future->taskId))
    {
        future->result = reduct_eval_call(reduct, callable, argc, argv);
        atomic_store_explicit(&future->done, true, memory_order_release);
    }

    return future;
}

REDUCT_API reduct_handle_t reduct_future_join(struct reduct* reduct, reduct_future_t* future)
{
    assert(reduct != NULL);
    assert(future != NULL);

    if (!atomic_load_explicit(&future->done, memory_order_acquire))
    {
        reduct_task_join(reduct, future->taskId);
    }

    if (future->error != NULL)
    {
        REDUCT_ERROR_RETHROW(reduct, future->error);
    }

    return future->result;
}

REDUCT_API bool reduct_future_is_done(struct reduct* reduct, reduct_future_t* future)
{
    assert(reduct != NULL);
    assert(future != NULL);

    REDUCT_UNUSED(reduct);

    return atomic_load_explicit(&future->done, memory_order_relaxed);
}
