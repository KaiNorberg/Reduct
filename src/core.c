#include "reduct/build.h"
#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>

#include <sys/stat.h>

static inline void reduct_state_init(reduct_t* reduct, reduct_error_t* error, reduct_env_t* env)
{
    reduct->env = env;

    reduct->error = error;
    error->reduct = reduct;

    reduct->nil = REDUCT_HANDLE_CREATE_LIST(reduct);

    reduct_atom_state_init(&reduct->atom);
    reduct_item_state_init(&reduct->item);
    reduct_gc_state_init(&reduct->gc);
    reduct_scratch_state_init(&reduct->scratch);
    reduct_eval_state_init(&reduct->eval);
}

REDUCT_API reduct_t* reduct_new(reduct_error_t* error)
{
    reduct_t* reduct = calloc(1, sizeof(reduct_t));
    if (reduct == NULL)
    {
        REDUCT_ERROR_GENERIC(NULL, error, NULL, INTERNAL, "out of memory");
    }

    reduct_env_t* env = calloc(1, sizeof(reduct_env_t));
    if (env == NULL)
    {
        free(reduct);
        REDUCT_ERROR_GENERIC(NULL, error, NULL, INTERNAL, "out of memory");
    }

    atomic_init(&env->refCount, 1);
    env->argc = 0;
    env->argv = NULL;

    reduct_rwmutex_init(&env->inputMutex);
    env->input = NULL;
    env->newInputId = 0;

    reduct_rwmutex_init(&env->importMutex);
    env->importPaths = NULL;
    env->importPathCount = 0;
    env->importPathCapacity = 0;

    reduct_rwmutex_init(&env->libMutex);
    env->libs = NULL;
    env->libCount = 0;
    env->libCapacity = 0;

    env->main = reduct;
    reduct_atom_env_init(&env->atom);
    reduct_native_env_init(&env->native);
    reduct_item_env_init(&env->item);
    reduct_gc_env_init(&env->gc);
    reduct_schema_env_init(&env->schema);
    reduct_optimize_env_init(&env->optimize);
    reduct_task_env_init(&env->task);

    reduct_state_init(reduct, error, env);

    reduct_build_register_intrinsics(reduct);

    char* envp = getenv("REDUCT_PATH");
    if (envp != NULL)
    {
        size_t envLen = strlen(envp);
        size_t start = 0;
        for (size_t pos = 0; pos <= envLen; pos++)
        {
            if (envp[pos] == ':' || envp[pos] == ';' || envp[pos] == '\0')
            {
                if (pos > start)
                {
                    size_t tokLen = pos - start;
                    char* token = (char*)malloc(tokLen + 1);
                    if (token != NULL)
                    {
                        memcpy(token, envp + start, tokLen);
                        token[tokLen] = '\0';
                        reduct_add_import_path(reduct, token);
                        free(token);
                    }
                }
                start = pos + 1;
            }
        }
    }

#if defined(__linux__) || defined(__APPLE__)
    reduct_add_import_path(reduct, "/usr/local/lib/reduct");
    reduct_add_import_path(reduct, "/usr/lib/reduct");
    reduct_add_import_path(reduct, "/lib/reduct");
#endif

    return reduct;
}

REDUCT_API reduct_t* reduct_new_thread(reduct_t* reduct, reduct_error_t* error)
{
    reduct_t* thread = calloc(1, sizeof(reduct_t));
    if (thread == NULL)
    {
        REDUCT_ERROR_GENERIC(reduct, error, NULL, INTERNAL, "out of memory");
    }

    atomic_fetch_add(&reduct->env->refCount, 1);
    reduct_state_init(thread, error, reduct->env);

    return thread;
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

    reduct_atom_state_deinit(&reduct->atom);
    reduct_item_state_deinit(&reduct->item);
    reduct_gc_state_deinit(&reduct->gc);
    reduct_scratch_state_deinit(&reduct->scratch);
    reduct_eval_state_deinit(&reduct->eval);

    if (atomic_fetch_sub(&reduct->env->refCount, 1) == 1)
    {
        reduct_atom_env_deinit(&reduct->env->atom);
        reduct_native_env_deinit(&reduct->env->native);
        reduct_item_env_deinit(reduct, &reduct->env->item);
        reduct_gc_env_deinit(&reduct->env->gc);
        reduct_schema_env_deinit(&reduct->env->schema);
        reduct_optimize_env_deinit(&reduct->env->optimize);
        reduct_task_env_deinit(&reduct->env->task);

        reduct_rwmutex_destroy(&reduct->env->inputMutex);
        reduct_input_t* input = reduct->env->input;
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

        reduct_rwmutex_destroy(&reduct->env->importMutex);
        if (reduct->env->importPaths != NULL)
        {
            for (size_t i = 0; i < reduct->env->importPathCount; i++)
            {
                free(reduct->env->importPaths[i]);
            }
            free(reduct->env->importPaths);
            reduct->env->importPaths = NULL;
            reduct->env->importPathCount = 0;
            reduct->env->importPathCapacity = 0;
        }

        reduct_rwmutex_destroy(&reduct->env->libMutex);
        if (reduct->env->libs != NULL)
        {
            for (size_t i = 0; i < reduct->env->libCount; i++)
            {
                if (reduct->env->libs[i] != NULL)
                {
                    REDUCT_LIB_CLOSE(reduct->env->libs[i]);
                }
            }
            free(reduct->env->libs);
        }

        free(reduct->env);
    }

    free(reduct);
}

REDUCT_API void reduct_env_lib_add(reduct_t* reduct, reduct_lib_t lib)
{
    assert(reduct != NULL);

    reduct_rwmutex_write_lock(&reduct->env->libMutex);
    if (reduct->env->libs == NULL)
    {
        reduct->env->libCapacity = REDUCT_LIBS_INITIAL;
        reduct->env->libs = (reduct_lib_t*)calloc(reduct->env->libCapacity, sizeof(reduct_lib_t));
    }
    else if (reduct->env->libCount >= reduct->env->libCapacity)
    {
        reduct->env->libCapacity *= REDUCT_LIBS_GROWTH;
        reduct_lib_t* newLibs =
            (reduct_lib_t*)realloc(reduct->env->libs, reduct->env->libCapacity * sizeof(reduct_lib_t));
        if (newLibs == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->env->libMutex);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->env->libs = newLibs;
    }
    reduct->env->libs[reduct->env->libCount++] = lib;
    reduct_rwmutex_write_unlock(&reduct->env->libMutex);
}

REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv)
{
    reduct->env->argc = argc;
    reduct->env->argv = argv;
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

    reduct_rwmutex_write_lock(&reduct->env->inputMutex);
    input->prev = reduct->env->input;
    input->id = reduct->env->newInputId++;
    reduct->env->input = input;
    reduct_rwmutex_write_unlock(&reduct->env->inputMutex);

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

    reduct_rwmutex_read_lock(&reduct->env->inputMutex);
    reduct_input_t* input = reduct->env->input;
    while (input != NULL)
    {
        if (input->id == id)
        {
            reduct_rwmutex_read_unlock(&reduct->env->inputMutex);
            return input;
        }
        input = input->prev;
    }
    reduct_rwmutex_read_unlock(&reduct->env->inputMutex);

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

    reduct_rwmutex_write_lock(&reduct->env->importMutex);
    if (reduct->env->importPaths == NULL)
    {
        reduct->env->importPathCapacity = REDUCT_IMPORT_PATHS_INITIAL;
        reduct->env->importPaths = (char**)malloc(reduct->env->importPathCapacity * sizeof(char*));
        if (reduct->env->importPaths == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->env->importMutex);
            free(pathCopy);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    else if (reduct->env->importPathCount >= reduct->env->importPathCapacity)
    {
        reduct->env->importPathCapacity *= REDUCT_IMPORT_PATHS_GROWTH;
        char** newPaths = (char**)realloc(reduct->env->importPaths, reduct->env->importPathCapacity * sizeof(char*));
        if (newPaths == NULL)
        {
            reduct_rwmutex_write_unlock(&reduct->env->importMutex);
            free(pathCopy);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->env->importPaths = newPaths;
    }

    reduct->env->importPaths[reduct->env->importPathCount++] = pathCopy;
    reduct_rwmutex_write_unlock(&reduct->env->importMutex);
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
        reduct_rwmutex_read_lock(&reduct->env->importMutex);
        for (size_t i = 0; i < reduct->env->importPathCount; i++)
        {
            const char* includeDir = reduct->env->importPaths[i];
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
                    reduct_rwmutex_read_unlock(&reduct->env->importMutex);
                    return;
                }
                struct stat st;
                if (stat(outPath, &st) == 0)
                {
                    reduct_rwmutex_read_unlock(&reduct->env->importMutex);
                    return;
                }
            }
        }
        reduct_rwmutex_read_unlock(&reduct->env->importMutex);
    }

    strncpy(outPath, normalized, maxLen - 1);
    outPath[maxLen - 1] = '\0';
}
