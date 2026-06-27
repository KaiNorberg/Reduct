#include "reduct/arena.h"
#include "reduct/build.h"
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>
#include <reduct/module.h>

static size_t reduct_get_cpu_count(void)
{
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors > 0 ? (size_t)sysinfo.dwNumberOfProcessors : 1;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? (size_t)nprocs : 1;
#endif
}

REDUCT_API reduct_t* reduct_new(void)
{
    reduct_global_t* global = calloc(1, sizeof(reduct_global_t));
    if (global == NULL)
    {
        return NULL;
    }

    global->argc = 0;
    global->argv = NULL;

    reduct_module_global_init(&global->module);

    global->threadCount = reduct_get_cpu_count();
    global->threads = calloc(global->threadCount, sizeof(reduct_t));
    if (global->threads == NULL)
    {
        free(global);
        return NULL;
    }

    reduct_atom_global_init(&global->atom);
    reduct_native_global_init(&global->native);
    reduct_item_global_init(&global->item);
    reduct_gc_global_init(&global->gc);
    reduct_schema_global_init(&global->schema);
    reduct_optimize_global_init(&global->optimize);
    reduct_task_global_init(&global->task);

    for (size_t i = 0; i < global->threadCount; i++)
    {
        reduct_t* thread = &global->threads[i];

        thread->global = global;
        reduct_arena_local_init(&thread->arena);
        reduct_item_local_init(&thread->item);
        reduct_scratch_local_init(&thread->scratch);
        reduct_eval_local_init(&thread->eval);
    }

    reduct_t* master = &global->threads[0];
    master->thrd = thrd_current();
    global->nil = REDUCT_HANDLE_CREATE_LIST(master, 0);

    for (size_t i = 1; i < global->threadCount; i++)
    {
        reduct_t* thread = &global->threads[i];
        if (thrd_create(&thread->thrd, reduct_task_worker, thread) != thrd_success)
        {
            free(global->threads);
            free(global);
            return NULL;
        }
    }

    reduct_build_register_intrinsics(master);

    return master;
}

REDUCT_API void reduct_userdata_set(reduct_t* reduct, void* userdata)
{
    reduct->userdata = userdata;
}

REDUCT_API void* reduct_userdata_get(reduct_t* reduct)
{
    return reduct->userdata;
}

REDUCT_API void reduct_free(reduct_t* reduct)
{
    if (reduct == NULL)
    {
        return;
    }

    reduct_global_t* global = reduct->global;

    reduct_task_global_deinit(&global->task);

    for (size_t i = 0; i < global->threadCount; i++)
    {
        reduct_t* thread = &global->threads[i];
        if (!thrd_equal(thread->thrd, thrd_current()))
        {
            thrd_join(thread->thrd, NULL);
        }
    }

    reduct_item_global_deinit(reduct, &global->item);
    reduct_atom_global_deinit(&global->atom);
    reduct_native_global_deinit(&global->native);
    reduct_gc_global_deinit(&global->gc);
    reduct_schema_global_deinit(&global->schema);
    reduct_optimize_global_deinit(&global->optimize);

    for (size_t i = 0; i < global->threadCount; i++)
    {
        reduct_t* thread = &global->threads[i];
        reduct_arena_local_deinit(&thread->arena);
        reduct_item_local_deinit(&thread->item);
        reduct_scratch_local_deinit(&thread->scratch);
        reduct_eval_local_deinit(&thread->eval);
    }

    reduct_module_global_deinit(&global->module);

    free(global->threads);
    free(global);
}

REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv)
{
    reduct->global->argc = argc;
    reduct->global->argv = argv;
}
