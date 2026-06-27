#include <reduct/core.h>
#include <reduct/module.h>
#include <reduct/parse.h>

#include <assert.h>
#include <string.h>
#include <sys/stat.h>

REDUCT_API void reduct_module_global_init(reduct_module_global_t* global)
{
    reduct_rwmutex_init(&global->moduleMutex);
    global->moduleHead = NULL;
    global->moduleNextId = 0;
    reduct_rwmutex_init(&global->pathsMutex);
    global->paths = NULL;
    global->pathCount = 0;
    global->pathCapacity = 0;
}

REDUCT_API void reduct_module_global_deinit(reduct_module_global_t* global)
{
    reduct_rwmutex_destroy(&global->moduleMutex);
    reduct_module_t* module = global->moduleHead;
    while (module != NULL)
    {
        reduct_module_t* next = module->prev;
        if (module->flags & REDUCT_MODULE_FLAG_BUFFER_OWNED)
        {
            free((void*)module->buffer);
        }
        if (module->path != NULL)
        {
            free(module->path);
        }
        if (module->flags & REDUCT_MODULE_FLAG_IS_LIBRARY)
        {
            if (module->lib != NULL)
            {
                REDUCT_LIB_CLOSE(module->lib);
            }
        }
        free(module);
        module = next;
    }

    reduct_rwmutex_destroy(&global->pathsMutex);
    if (global->paths != NULL)
    {
        for (size_t i = 0; i < global->pathCount; i++)
        {
            free(global->paths[i]);
        }
        free(global->paths);
    }
}

static reduct_module_t* reduct_module_new_raw(struct reduct* reduct, const char* path)
{
    assert(reduct != NULL);
    assert(path != NULL);

    reduct_module_t* module = malloc(sizeof(reduct_module_t));
    if (module == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    module->buffer = NULL;
    module->end = NULL;
    module->lib = NULL;
    module->flags = REDUCT_MODULE_FLAG_NONE;

    size_t pathLen = strlen(path);
    module->path = malloc(pathLen + 1);
    if (module->path == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memcpy(module->path, path, pathLen + 1);

    reduct_module_global_t* global = &reduct->global->module;
    reduct_rwmutex_write_lock(&global->moduleMutex);
    module->prev = global->moduleHead;
    module->id = global->moduleNextId++;
    global->moduleHead = module;
    reduct_rwmutex_write_unlock(&global->moduleMutex);

    return module;
}

REDUCT_API reduct_module_t* reduct_module_new(struct reduct* reduct, const char* buffer, size_t length,
    const char* path, reduct_module_flags_t flags)
{
    assert(reduct != NULL);
    assert(buffer != NULL);
    assert(path != NULL);

    reduct_module_t* module = reduct_module_new_raw(reduct, path);
    module->buffer = buffer;
    module->end = buffer + length;
    module->flags = flags;

    return module;
}

REDUCT_API reduct_module_t* reduct_module_lookup(reduct_t* reduct, reduct_module_id_t id)
{
    assert(reduct != NULL);

    reduct_module_global_t* global = &reduct->global->module;
    reduct_rwmutex_read_lock(&global->moduleMutex);

    reduct_module_t* module = global->moduleHead;
    while (module != NULL)
    {
        if (module->id == id)
        {
            reduct_rwmutex_read_unlock(&global->moduleMutex);
            return module;
        }
        module = module->prev;
    }
    reduct_rwmutex_read_unlock(&global->moduleMutex);

    return NULL;
}

REDUCT_API void reduct_module_add_path(reduct_t* reduct, const char* path)
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

    reduct_module_global_t* global = &reduct->global->module;

    reduct_rwmutex_write_lock(&global->pathsMutex);
    if (global->pathCount >= global->pathCapacity)
    {
        global->pathCapacity =
            global->pathCapacity == 0 ? REDUCT_MODULE_PATHS_INITIAL : global->pathCapacity * REDUCT_MODULE_PATHS_GROWTH;
        char** newPaths = (char**)realloc(global->paths, global->pathCapacity * sizeof(char*));
        if (newPaths == NULL)
        {
            reduct_rwmutex_write_unlock(&global->pathsMutex);
            free(pathCopy);
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        global->paths = newPaths;
    }

    global->paths[global->pathCount++] = pathCopy;
    reduct_rwmutex_write_unlock(&global->pathsMutex);
}

static void reduct_module_path_normalize(char* path, size_t* outLen)
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

REDUCT_API void reduct_module_resolve_path(reduct_t* reduct, const char* path, size_t pathLen, char* outPath,
    size_t maxLen, bool checkExistence)
{
    char normalized[REDUCT_PATH_MAX];
    REDUCT_ERROR_ASSERT(reduct, pathLen < REDUCT_PATH_MAX, "path exceeds maximum length");
    memcpy(normalized, path, pathLen);
    normalized[pathLen] = '\0';
    reduct_module_path_normalize(normalized, &pathLen);

    if (pathLen > 0 && (normalized[0] == '/'))
    {
        strncpy(outPath, normalized, maxLen - 1);
        outPath[maxLen - 1] = '\0';
        return;
    }

    reduct_module_global_t* global = &reduct->global->module;

    if (reduct != NULL && reduct->eval.frameCount > 0)
    {
        for (size_t i = reduct->eval.frameCount; i > 0; i--)
        {
            reduct_eval_frame_t* frame = &reduct->eval.frames[i - 1];
            reduct_item_t* funcItem = REDUCT_CONTAINER_OF(frame->closure->function, reduct_item_t, function);
            reduct_module_t* funcModule = reduct_module_lookup(reduct, funcItem->moduleId);
            if (funcModule != NULL && funcModule->path[0] != '\0')
            {
                const char* lastSlash = strrchr(funcModule->path, '/');
                if (!lastSlash)
                {
                    lastSlash = strrchr(funcModule->path, '\\');
                }

                if (lastSlash != NULL)
                {
                    size_t dirLen = (size_t)(lastSlash - funcModule->path) + 1;
                    if (dirLen + pathLen < maxLen)
                    {
                        memcpy(outPath, funcModule->path, dirLen);
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
        reduct_rwmutex_read_lock(&global->pathsMutex);
        for (size_t i = 0; i < global->pathCount; i++)
        {
            const char* includeDir = global->paths[i];
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
                    reduct_rwmutex_read_unlock(&global->pathsMutex);
                    return;
                }
                struct stat st;
                if (stat(outPath, &st) == 0)
                {
                    reduct_rwmutex_read_unlock(&global->pathsMutex);
                    return;
                }
            }
        }
        reduct_rwmutex_read_unlock(&global->pathsMutex);
    }

    strncpy(outPath, normalized, maxLen - 1);
    outPath[maxLen - 1] = '\0';
}

REDUCT_API reduct_handle_t reduct_module_import(struct reduct* reduct, reduct_handle_t path, reduct_handle_t compiler,
    reduct_handle_t compilerArgs)
{
    assert(reduct != NULL);
    char libPathBuf[REDUCT_PATH_MAX];
    char pathBuf[REDUCT_PATH_MAX];
    reduct_lib_t lib;

    const char* pathStr;
    size_t pathLen;
    reduct_handle_atom_string(reduct, &path, &pathStr, &pathLen);
    reduct_module_resolve_path(reduct, pathStr, pathLen, pathBuf, REDUCT_PATH_MAX, true);
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

        reduct_module_init_fn init = (reduct_module_init_fn)REDUCT_LIB_SYM(lib, REDUCT_MODULE_ENTRY);
        if (init == NULL)
        {
            REDUCT_LIB_CLOSE(lib);
            REDUCT_ERROR_THROW(reduct, "could not find %s in %s", REDUCT_MODULE_ENTRY, pathString);
        }

        reduct_module_t* module = reduct_module_new_raw(reduct, pathString);
        module->lib = lib;
        module->flags = REDUCT_MODULE_FLAG_IS_LIBRARY;
        
        reduct_handle_t handle = init(reduct);

        reduct_list_t* wrapper = reduct_list_new(reduct, 2);
        wrapper->handles[0] = REDUCT_HANDLE_CREATE_SYMBOL(reduct, "do");
        wrapper->handles[1] = handle;

        return REDUCT_HANDLE_FROM_LIST(wrapper);
    }

    return reduct_parse_file(reduct, pathString);
}
