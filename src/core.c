#include "reduct/atom.h"
#include "reduct/core.h"
#include "reduct/eval.h"
#include "reduct/gc.h"
#include "reduct/item.h"

REDUCT_API reduct_t* reduct_new(reduct_error_t* error)
{
    reduct_t* reduct = REDUCT_CALLOC(1, sizeof(reduct_t));
    if (reduct == REDUCT_NULL)
    {
        REDUCT_ERROR_THROW(REDUCT_NULL, error, REDUCT_NULL, INTERNAL, "out of memory");
    }
    reduct->error = error;
    error->reduct = reduct;

    reduct->trueItem = REDUCT_CONTAINER_OF(reduct_atom_new_int(reduct, 1), reduct_item_t, atom);
    reduct->falseItem = REDUCT_CONTAINER_OF(reduct_atom_new_int(reduct, 0), reduct_item_t, atom);
    reduct->nilItem = REDUCT_CONTAINER_OF(reduct_list_new(reduct), reduct_item_t, list);
    reduct->piItem = REDUCT_CONTAINER_OF(reduct_atom_new_float(reduct, REDUCT_PI), reduct_item_t, atom);
    reduct->eItem = REDUCT_CONTAINER_OF(reduct_atom_new_float(reduct, REDUCT_E), reduct_item_t, atom);

    reduct->falseItem->flags |= REDUCT_ITEM_FLAG_FALSY;
    reduct->nilItem->flags |= REDUCT_ITEM_FLAG_FALSY;
    reduct->trueItem->flags &= ~REDUCT_ITEM_FLAG_FALSY;

    reduct_constant_register(reduct, "true", reduct->trueItem);
    reduct_constant_register(reduct, "false", reduct->falseItem);
    reduct_constant_register(reduct, "nil", reduct->nilItem);
    reduct_constant_register(reduct, "pi", reduct->piItem);
    reduct_constant_register(reduct, "e", reduct->eItem);

    reduct_intrinsic_register_all(reduct);

    reduct->argc = 0;
    reduct->argv = REDUCT_NULL;

    reduct->importPaths = REDUCT_NULL;
    reduct->importPathCount = 0;
    reduct->importPathCapacity = 0;

    char* env = REDUCT_GETENV("REDUCT_PATH");
    if (env != REDUCT_NULL)
    {
        reduct_size_t envLen = REDUCT_STRLEN(env);
        reduct_size_t start = 0;
        for (reduct_size_t pos = 0; pos <= envLen; pos++)
        {
            if (env[pos] == ':' || env[pos] == ';' || env[pos] == '\0')
            {
                if (pos > start)
                {
                    reduct_size_t tokLen = pos - start;
                    char* token = (char*)REDUCT_MALLOC(tokLen + 1);
                    if (token != REDUCT_NULL)
                    {
                        REDUCT_MEMCPY(token, env + start, tokLen);
                        token[tokLen] = '\0';
                        reduct_add_import_path(reduct, token);
                        REDUCT_FREE(token);
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

REDUCT_API void reduct_free(reduct_t* reduct)
{
    if (reduct == REDUCT_NULL)
    {
        return;
    }

    for (reduct_size_t i = 0; i < reduct->scratchCapacity; i++)
    {
        if (reduct->scratch[i].buffer != REDUCT_NULL)
        {
            REDUCT_FREE(reduct->scratch[i].buffer);
        }
    }
    reduct->scratchCapacity = 0;

    if (reduct->atomMap != REDUCT_NULL)
    {
        REDUCT_FREE(reduct->atomMap);
        reduct->atomMap = REDUCT_NULL;
        reduct->atomMapCapacity = 0;
        reduct->atomMapSize = 0;
    }

    if (reduct->nativeMap != REDUCT_NULL)
    {
        for (reduct_size_t i = 0; i < reduct->nativeMapCapacity; i++)
        {
            if (reduct->nativeMap[i].name != REDUCT_NULL)
            {
                REDUCT_FREE(reduct->nativeMap[i].name);
            }
        }
        REDUCT_FREE(reduct->nativeMap);
        reduct->nativeMap = REDUCT_NULL;
        reduct->nativeMapCapacity = 0;
        reduct->nativeMapSize = 0;
    }

    reduct_item_block_t* block = reduct->block;
    while (block != REDUCT_NULL)
    {
        reduct_item_block_t* next = block->next;
        for (int i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            reduct_item_t* item = &block->items[i];
            reduct_item_free(reduct, item);
        }

        REDUCT_FREE(block->allocated);
        block = next;
    }

    reduct_input_t* input = reduct->input;
    while (input != REDUCT_NULL)
    {
        reduct_input_t* next = input->prev;
        if (input->flags & REDUCT_INPUT_FLAG_OWNED)
        {
            REDUCT_FREE((void*)input->buffer);
        }
        if (input != &reduct->firstInput)
        {
            REDUCT_FREE(input);
        }
        input = next;
    }

    if (reduct->frames != REDUCT_NULL)
    {
        REDUCT_FREE(reduct->frames);
        reduct->frames = REDUCT_NULL;
        reduct->frameCount = 0;
        reduct->frameCapacity = 0;
    }

    if (reduct->regs != REDUCT_NULL)
    {
        REDUCT_FREE(reduct->regs);
        reduct->regs = REDUCT_NULL;
        reduct->regCount = 0;
        reduct->regCapacity = 0;
    }

    if (reduct->libs != REDUCT_NULL)
    {
        for (reduct_size_t i = 0; i < reduct->libCapacity; i++)
        {
            if (reduct->libs[i] != REDUCT_NULL)
            {
                REDUCT_LIB_CLOSE(reduct->libs[i]);
            }
        }
        REDUCT_FREE(reduct->libs);
        reduct->libs = REDUCT_NULL;
        reduct->libCount = 0;
        reduct->libCapacity = 0;
    }

    if (reduct->importPaths != REDUCT_NULL)
    {
        for (reduct_size_t i = 0; i < reduct->importPathCount; i++)
        {
            REDUCT_FREE(reduct->importPaths[i]);
        }
        REDUCT_FREE(reduct->importPaths);
        reduct->importPaths = REDUCT_NULL;
        reduct->importPathCount = 0;
        reduct->importPathCapacity = 0;
    }

    if (reduct->retained != REDUCT_NULL)
    {
        REDUCT_FREE(reduct->retained);
        reduct->retained = REDUCT_NULL;
        reduct->retainedCount = 0;
        reduct->retainedCapacity = 0;
    }

    REDUCT_FREE(reduct);
}

REDUCT_API void reduct_args_set(reduct_t* reduct, int argc, char** argv)
{
    reduct->argc = argc;
    reduct->argv = argv;
}

REDUCT_API void reduct_constant_register(reduct_t* reduct, const char* name, reduct_item_t* item)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(name != REDUCT_NULL);
    REDUCT_ASSERT(item != REDUCT_NULL);

    if (reduct->constantCount >= REDUCT_CONSTANTS_MAX)
    {
        REDUCT_ERROR_INTERNAL(reduct, "too many constants, limit is %u", REDUCT_CONSTANTS_MAX);
    }

    reduct_size_t len = REDUCT_STRLEN(name);
    reduct_atom_t* atom = reduct_atom_lookup(reduct, name, len, REDUCT_ATOM_LOOKUP_NONE);

    reduct->constants[reduct->constantCount].name = atom;
    reduct->constants[reduct->constantCount].item = item;
    reduct->constantCount++;
}

REDUCT_API reduct_input_t* reduct_input_new(reduct_t* reduct, const char* buffer, reduct_size_t length,
    const char* path, reduct_input_flags_t flags)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(buffer != REDUCT_NULL);
    REDUCT_ASSERT(path != REDUCT_NULL);

    reduct_input_t* input;
    if (reduct->input == REDUCT_NULL)
    {
        input = &reduct->firstInput;
    }
    else
    {
        input = REDUCT_MALLOC(sizeof(reduct_input_t));
        if (input == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    input->prev = reduct->input;
    input->buffer = buffer;
    input->end = buffer + length;
    input->id = reduct->newInputId++;
    input->flags = flags;
    REDUCT_STRNCPY(input->path, path, REDUCT_PATH_MAX - 1);
    input->path[REDUCT_PATH_MAX - 1] = '\0';
    reduct->input = input;
    return input;
}

REDUCT_API reduct_input_t* reduct_input_lookup(reduct_t* reduct, reduct_input_id_t id)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_input_t* input = reduct->input;
    while (input != REDUCT_NULL)
    {
        if (input->id == id)
        {
            return input;
        }
        input = input->prev;
    }

    return REDUCT_NULL;
}

REDUCT_API void reduct_add_import_path(reduct_t* reduct, const char* path)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(path != REDUCT_NULL);

    if (reduct->importPaths == REDUCT_NULL)
    {
        reduct->importPathCapacity = REDUCT_IMPORT_PATHS_INITIAL;
        reduct->importPaths = (char**)REDUCT_MALLOC(reduct->importPathCapacity * sizeof(char*));
        if (reduct->importPaths == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }
    else if (reduct->importPathCount >= reduct->importPathCapacity)
    {
        reduct->importPathCapacity *= REDUCT_IMPORT_PATHS_GROWTH;
        char** newPaths = (char**)REDUCT_REALLOC(reduct->importPaths, reduct->importPathCapacity * sizeof(char*));
        if (newPaths == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        reduct->importPaths = newPaths;
    }

    reduct_size_t len = REDUCT_STRLEN(path);
    char* pathCopy = (char*)REDUCT_MALLOC(len + 1);
    if (pathCopy == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }
    REDUCT_MEMCPY(pathCopy, path, len);
    pathCopy[len] = '\0';

    reduct->importPaths[reduct->importPathCount++] = pathCopy;
}