#include "reduct/build.h"
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>

#include <sys/stat.h>

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

static inline void reduct_input_global_init(reduct_input_global_t* global)
{
    reduct_rwmutex_init(&global->mutex);
    global->head = NULL;
    global->nextId = 0;
}

static inline void reduct_input_global_deinit(reduct_input_global_t* global)
{
    reduct_rwmutex_destroy(&global->mutex);
    reduct_input_t* input = global->head;
    while (input != NULL)
    {
        reduct_input_t* next = input->prev;
        if (input->flags & REDUCT_INPUT_FLAG_OWNED)
        {
            free((void*)input->buffer);
        }
        free(input);
        input = next;
    }
}

static inline void reduct_import_global_init(reduct_import_global_t* global)
{
    reduct_rwmutex_init(&global->mutex);
    global->paths = NULL;
    global->count = 0;
    global->capacity = 0;
}

static inline void reduct_import_global_deinit(reduct_import_global_t* global)
{
    reduct_rwmutex_destroy(&global->mutex);
    if (global->paths != NULL)
    {
        for (size_t i = 0; i < global->count; i++)
        {
            free(global->paths[i]);
        }
        free(global->paths);
    }
}

static inline void reduct_lib_global_init(reduct_lib_global_t* global)
{
    reduct_rwmutex_init(&global->mutex);
    global->array = NULL;
    global->count = 0;
    global->capacity = 0;
}

static inline void reduct_lib_global_deinit(reduct_t* reduct, reduct_lib_global_t* global)
{
    REDUCT_UNUSED(reduct);
    reduct_rwmutex_destroy(&global->mutex);
    if (global->array != NULL)
    {
        for (size_t i = 0; i < global->count; i++)
        {
            if (global->array[i] != NULL)
            {
                REDUCT_LIB_CLOSE(global->array[i]);
            }
        }
        free(global->array);
    }
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

    reduct_input_global_init(&global->input);
    reduct_import_global_init(&global->import);
    reduct_lib_global_init(&global->lib);

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
        reduct_atom_local_init(&thread->atom);
        reduct_item_local_init(&thread->item);
        reduct_scratch_local_init(&thread->scratch);
        reduct_eval_local_init(&thread->eval);
    }

    reduct_t* master = &global->threads[0];
    master->thrd = thrd_current();
    global->nil = REDUCT_HANDLE_CREATE_LIST(master);

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
        if (thrd_equal(thread->thrd, thrd_current()))
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
        reduct_atom_local_deinit(&thread->atom);
        reduct_item_local_deinit(&thread->item);
        reduct_scratch_local_deinit(&thread->scratch);
        reduct_eval_local_deinit(&thread->eval);
    }

    reduct_input_global_deinit(&global->input);
    reduct_import_global_deinit(&global->import);
    reduct_lib_global_deinit(reduct, &global->lib);

    free(global->threads);
    free(global);
}

REDUCT_API void reduct_global_lib_add(reduct_t* reduct, reduct_lib_t lib)
{
    assert(reduct != NULL);

    reduct_rwmutex_write_lock(&reduct->global->lib.mutex);
    if (reduct->global->lib.array == NULL)
    {
        reduct->global->lib.capacity = REDUCT_LIBS_INITIAL;
        reduct->global->lib.array = (reduct_lib_t*)calloc(reduct->global->lib.capacity, sizeof(reduct_lib_t));
        if (reduct->global->lib.array == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->lib.mutex);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    else if (reduct->global->lib.count >= reduct->global->lib.capacity)
    {
        reduct->global->lib.capacity *= REDUCT_LIBS_GROWTH;
        reduct_lib_t* newLibs =
            (reduct_lib_t*)realloc(reduct->global->lib.array, reduct->global->lib.capacity * sizeof(reduct_lib_t));
        if (newLibs == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->lib.mutex);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->global->lib.array = newLibs;
    }
    reduct->global->lib.array[reduct->global->lib.count++] = lib;
    reduct_rwmutex_write_unlock(&reduct->global->lib.mutex);
}

REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv)
{
    reduct->global->argc = argc;
    reduct->global->argv = argv;
}

REDUCT_API reduct_input_t* reduct_input_new(reduct_t* reduct, const char* buffer, size_t length, const char* path,
    reduct_input_flags_t flags)
{
    assert(reduct != NULL);
    assert(buffer != NULL);
    assert(path != NULL);

    reduct_input_t* input = malloc(sizeof(reduct_input_t));
    if (input == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    reduct_rwmutex_write_lock(&reduct->global->input.mutex);
    input->prev = reduct->global->input.head;
    input->id = reduct->global->input.nextId++;
    reduct->global->input.head = input;
    reduct_rwmutex_write_unlock(&reduct->global->input.mutex);

    input->buffer = buffer;
    input->end = buffer + length;
    input->flags = flags;
    strncpy(input->path, path, REDUCT_PATH_MAX - 1);
    input->path[REDUCT_PATH_MAX - 1] = '\0';
    return input;
}

REDUCT_API reduct_input_t* reduct_input_lookup(reduct_t* reduct, reduct_input_id_t id)
{
    assert(reduct != NULL);

    reduct_rwmutex_read_lock(&reduct->global->input.mutex);
    reduct_input_t* input = reduct->global->input.head;
    while (input != NULL)
    {
        if (input->id == id)
        {
            reduct_rwmutex_read_unlock(&reduct->global->input.mutex);
            return input;
        }
        input = input->prev;
    }
    reduct_rwmutex_read_unlock(&reduct->global->input.mutex);

    return NULL;
}

REDUCT_API void reduct_add_import_path(reduct_t* reduct, const char* path)
{
    assert(reduct != NULL);
    assert(path != NULL);

    size_t len = strlen(path);
    char* pathCopy = (char*)malloc(len + 1);
    if (pathCopy == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memcpy(pathCopy, path, len);
    pathCopy[len] = '\0';

    reduct_rwmutex_write_lock(&reduct->global->import.mutex);
    if (reduct->global->import.paths == NULL)
    {
        reduct->global->import.capacity = REDUCT_IMPORT_PATHS_INITIAL;
        reduct->global->import.paths = (char**)malloc(reduct->global->import.capacity * sizeof(char*));
        if (reduct->global->import.paths == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->import.mutex);
            free(pathCopy);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    else if (reduct->global->import.count >= reduct->global->import.capacity)
    {
        reduct->global->import.capacity *= REDUCT_IMPORT_PATHS_GROWTH;
        char** newPaths =
            (char**)realloc(reduct->global->import.paths, reduct->global->import.capacity * sizeof(char*));
        if (newPaths == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->global->import.mutex);
            free(pathCopy);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->global->import.paths = newPaths;
    }

    reduct->global->import.paths[reduct->global->import.count++] = pathCopy;
    reduct_rwmutex_write_unlock(&reduct->global->import.mutex);
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
        {
            break;
        }

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

REDUCT_API void reduct_resolve_path(reduct_t* reduct, const char* path, size_t pathLen, char* outPath, size_t maxLen,
    bool checkExistence)
{
    char normalized[REDUCT_PATH_MAX];
    REDUCT_ERROR_ASSERT(reduct, pathLen < REDUCT_PATH_MAX, "path exceeds maximum length");
    memcpy(normalized, path, pathLen);
    normalized[pathLen] = '\0';
    reduct_path_normalize(normalized, &pathLen);

    if (pathLen > 0 && (normalized[0] == '/'))
    {
        strncpy(outPath, normalized, maxLen - 1);
        outPath[maxLen - 1] = '\0';
        return;
    }

    if (reduct != NULL && reduct->eval.frameCount > 0)
    {
        for (size_t i = reduct->eval.frameCount; i > 0; i--)
        {
            reduct_eval_frame_t* frame = &reduct->eval.frames[i - 1];
            reduct_item_t* funcItem = REDUCT_CONTAINER_OF(frame->closure->function, reduct_item_t, function);
            reduct_input_t* funcInput = reduct_input_lookup(reduct, funcItem->inputId);
            if (funcInput != NULL && funcInput->path[0] != '\0')
            {
                const char* lastSlash = strrchr(funcInput->path, '/');
                if (!lastSlash)
                {
                    lastSlash = strrchr(funcInput->path, '\\');
                }

                if (lastSlash != NULL)
                {
                    size_t dirLen = (size_t)(lastSlash - funcInput->path) + 1;
                    if (dirLen + pathLen < maxLen)
                    {
                        memcpy(outPath, funcInput->path, dirLen);
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
        }
    }

    if (reduct != NULL)
    {
        reduct_rwmutex_read_lock(&reduct->global->import.mutex);
        for (size_t i = 0; i < reduct->global->import.count; i++)
        {
            const char* includeDir = reduct->global->import.paths[i];
            size_t dirLen = strlen(includeDir);
            size_t needSep = (dirLen > 0 && includeDir[dirLen - 1] != '/' && includeDir[dirLen - 1] != '\\') ? 1 : 0;
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
                    reduct_rwmutex_read_unlock(&reduct->global->import.mutex);
                    return;
                }
                struct stat st;
                if (stat(outPath, &st) == 0)
                {
                    reduct_rwmutex_read_unlock(&reduct->global->import.mutex);
                    return;
                }
            }
        }
        reduct_rwmutex_read_unlock(&reduct->global->import.mutex);
    }

    strncpy(outPath, normalized, maxLen - 1);
    outPath[maxLen - 1] = '\0';
}
