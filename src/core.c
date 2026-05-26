#include <reduct/atom.h>
#include <reduct/core.h>
#include <reduct/eval.h>
#include <reduct/gc.h>
#include <reduct/item.h>

REDUCT_API reduct_t* reduct_new(reduct_error_t* error)
{
    reduct_t* reduct = calloc(1, sizeof(reduct_t));
    if (reduct == NULL)
    {
        REDUCT_ERROR_GENERIC(NULL, error, NULL, INTERNAL, "out of memory");
    }
    reduct->error = error;
    error->reduct = reduct;

    reduct->argc = 0;
    reduct->argv = NULL;

    reduct->importPaths = NULL;
    reduct->importPathCount = 0;
    reduct->importPathCapacity = 0;

    char* env = getenv("REDUCT_PATH");
    if (env != NULL)
    {
        size_t envLen = strlen(env);
        size_t start = 0;
        for (size_t pos = 0; pos <= envLen; pos++)
        {
            if (env[pos] == ':' || env[pos] == ';' || env[pos] == '\0')
            {
                if (pos > start)
                {
                    size_t tokLen = pos - start;
                    char* token = (char*)malloc(tokLen + 1);
                    if (token != NULL)
                    {
                        memcpy(token, env + start, tokLen);
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

    reduct->nil = REDUCT_HANDLE_CREATE_LIST(reduct);

    return reduct;
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

    if (reduct->libs != NULL)
    {
        for (size_t i = 0; i < reduct->libCount; i++)
        {
            if (reduct->libs[i] != NULL)
            {
                REDUCT_LIB_CLOSE(reduct->libs[i]);
            }
        }
        free(reduct->libs);
        reduct->libs = NULL;
        reduct->libCount = 0;
        reduct->libCapacity = 0;
    }

    for (size_t i = 0; i < reduct->scratchCapacity; i++)
    {
        if (reduct->scratch[i].buffer != NULL)
        {
            free(reduct->scratch[i].buffer);
        }
    }
    reduct->scratchCapacity = 0;

    if (reduct->atomMap != NULL)
    {
        free(reduct->atomMap);
        reduct->atomMap = NULL;
        reduct->atomMapCapacity = 0;
        reduct->atomMapSize = 0;
    }

    if (reduct->nativeMap != NULL)
    {
        for (size_t i = 0; i < reduct->nativeMapCapacity; i++)
        {
            if (reduct->nativeMap[i].name != NULL)
            {
                free(reduct->nativeMap[i].name);
            }
        }
        free(reduct->nativeMap);
        reduct->nativeMap = NULL;
        reduct->nativeMapCapacity = 0;
        reduct->nativeMapSize = 0;
    }

    reduct_item_block_t* block = reduct->block;
    while (block != NULL)
    {
        reduct_item_block_t* next = block->next;
        for (int i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            reduct_item_t* item = &block->items[i];
            reduct_item_free(reduct, item);
        }

        free(block->allocated);
        block = next;
    }

    reduct_input_t* input = reduct->input;
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

    if (reduct->frames != NULL)
    {
        free(reduct->frames);
        reduct->frames = NULL;
        reduct->frameCount = 0;
        reduct->frameCapacity = 0;
    }

    if (reduct->regs != NULL)
    {
        free(reduct->regs);
        reduct->regs = NULL;
        reduct->regCount = 0;
        reduct->regCapacity = 0;
    }

    if (reduct->importPaths != NULL)
    {
        for (size_t i = 0; i < reduct->importPathCount; i++)
        {
            free(reduct->importPaths[i]);
        }
        free(reduct->importPaths);
        reduct->importPaths = NULL;
        reduct->importPathCount = 0;
        reduct->importPathCapacity = 0;
    }

    if (reduct->retained != NULL)
    {
        free(reduct->retained);
        reduct->retained = NULL;
        reduct->retainedCount = 0;
        reduct->retainedCapacity = 0;
    }

    if (reduct->schemas != NULL)
    {
        for (size_t i = 0; i < reduct->schemaCount; i++)
        {
            if (reduct->schemas[i] != NULL)
            {
                free(reduct->schemas[i]);
            }
        }
        free(reduct->schemas);
        reduct->schemas = NULL;
        reduct->schemaCapacity = 0;
        reduct->schemaCount = 0;
    }

    free(reduct);
}

REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv)
{
    reduct->argc = argc;
    reduct->argv = argv;
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
    input->prev = reduct->input;
    input->buffer = buffer;
    input->end = buffer + length;
    input->id = reduct->newInputId++;
    input->flags = flags;
    strncpy(input->path, path, REDUCT_PATH_MAX - 1);
    input->path[REDUCT_PATH_MAX - 1] = '\0';
    reduct->input = input;
    return input;
}

REDUCT_API reduct_input_t* reduct_input_lookup(reduct_t* reduct, reduct_input_id_t id)
{
    assert(reduct != NULL);

    reduct_input_t* input = reduct->input;
    while (input != NULL)
    {
        if (input->id == id)
        {
            return input;
        }
        input = input->prev;
    }

    return NULL;
}

REDUCT_API void reduct_add_import_path(reduct_t* reduct, const char* path)
{
    assert(reduct != NULL);
    assert(path != NULL);

    if (reduct->importPaths == NULL)
    {
        reduct->importPathCapacity = REDUCT_IMPORT_PATHS_INITIAL;
        reduct->importPaths = (char**)malloc(reduct->importPathCapacity * sizeof(char*));
        if (reduct->importPaths == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    else if (reduct->importPathCount >= reduct->importPathCapacity)
    {
        reduct->importPathCapacity *= REDUCT_IMPORT_PATHS_GROWTH;
        char** newPaths = (char**)realloc(reduct->importPaths, reduct->importPathCapacity * sizeof(char*));
        if (newPaths == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->importPaths = newPaths;
    }

    size_t len = strlen(path);
    char* pathCopy = (char*)malloc(len + 1);
    if (pathCopy == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    memcpy(pathCopy, path, len);
    pathCopy[len] = '\0';

    reduct->importPaths[reduct->importPathCount++] = pathCopy;
}
