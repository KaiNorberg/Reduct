#define REDUCT_INLINE
#include <reduct/reduct.h>
#include "reduct_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{    
    static int result = 0;
    const char* irOutputFile = NULL;
    int isSilent = 0;
    int parseOnly = 0;
    int shouldDump = 0;
    reduct_optimize_flags_t optimizeFlags = REDUCT_OPTIMIZE_O3;
    const char* evalExpr = NULL;
    const char* filename = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--silent") == 0)
        {
            isSilent = 1;
        }
        else if (strcmp(argv[i], "-e") == 0)
        {
            if (i + 1 < argc)
            {
                evalExpr = argv[++i];
            }
            else
            {
                fprintf(stderr, "error: -e requires an expression argument\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-I") == 0)
        {
            if (i + 1 < argc)
            {
                i++;
            }
            else
            {
                fprintf(stderr, "error: -I requires a path argument\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            shouldDump = 1;
        }
        else if (strcmp(argv[i], "--ir") == 0)
        {
            if (i + 1 < argc)
            {
                irOutputFile = argv[++i];
            }
            else
            {
                fprintf(stderr, "error: --ir requires an output file argument\n");
                return 1;
            }
        }
        else if (strncmp(argv[i], "-O", 2) == 0)
        {
            const char* level = argv[i] + 2;
            if (*level == '\0')
            {
                optimizeFlags = REDUCT_OPTIMIZE_O3;
            }
            else if (strcmp(level, "0") == 0)
            {
                optimizeFlags = REDUCT_OPTIMIZE_NONE;
            }
            else if (strcmp(level, "1") == 0)
            {
                optimizeFlags = REDUCT_OPTIMIZE_O1;
            }
            else if (strcmp(level, "2") == 0)
            {
                optimizeFlags = REDUCT_OPTIMIZE_O2;
            }
            else if (strcmp(level, "3") == 0)
            {
                optimizeFlags = REDUCT_OPTIMIZE_O3;
            }
            else
            {
                fprintf(stderr, "error: unknown optimization level '%s'\n", level);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parse-only") == 0)
        {
            parseOnly = 1;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            printf("Reduct %s\n", REDUCT_FULL_VERSION_STRING);
            return 0;
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return 1;
        }
        else
        {
            filename = argv[i];
        }
    }

    if (filename == NULL && evalExpr == NULL)
    {
        fprintf(stderr, "Reduct %s\n", REDUCT_FULL_VERSION_STRING);
        fprintf(stderr, "Usage: %s [options] <filename>\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -e <expr>      Evaluate the given expression\n");
        fprintf(stderr, "  -s, --silent   Do not print the evaluation result\n");
        fprintf(stderr, "  -I <path>      Add a directory to the import search path\n");
        fprintf(stderr, "  -d             Output the compiled bytecode (disassemble)\n");
        fprintf(stderr, "  --ir <file>    Output the IR graph (RVSDG) to a .dot file\n");
        fprintf(stderr, "  -O<level>      Set optimization level (0-3, default 3)\n");
        fprintf(stderr, "  -v, --version  Output version information\n");        
        fprintf(stderr, "Environment Variables:\n");
        fprintf(stderr, "  REDUCT_PATH    Colon-separated or semi-colon-separated list of directories for imports\n");
        return 1;
    }

    reduct_t* reduct = reduct_new();
    if (reduct == NULL)
    {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    reduct_error_t error = REDUCT_ERROR();
    REDUCT_ERROR_TRY(reduct, &error)
    {
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

        for (int i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "-I") == 0 && i + 1 < argc)
            {
                reduct_add_import_path(reduct, argv[++i]);
            }
            else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--ir") == 0) && i + 1 < argc)
            {
                i++;
            }
        }

        reduct_args_set(reduct, argc, argv);

        reduct_handle_t ast = REDUCT_HANDLE_NIL(reduct);
        if (evalExpr != NULL)
        {
            ast = reduct_parse(reduct, evalExpr, strlen(evalExpr), "<eval>");
        }
        else if (filename != NULL)
        {
            ast = reduct_parse_file(reduct, filename);
        }

        static char buffer[0x10000];

        if (parseOnly)
        {    
            reduct_stringify(reduct, ast, buffer, sizeof(buffer));
            printf("%s", buffer);
            goto cleanup;
        }
        
        reduct_stdlib_register(reduct, REDUCT_STDLIB_ALL);
        
        reduct_handle_t node = reduct_build(reduct, ast);
        reduct_optimize(reduct, node, optimizeFlags);

        if (irOutputFile != NULL)
        {
            FILE* out = fopen(irOutputFile, "w");
            if (out == NULL)
            {
                fprintf(stderr, "error: could not open '%s' for writing\n", irOutputFile);
                result = 1;
                goto cleanup;
            }
            reduct_dump_rvsdg(reduct, node, out);
            fclose(out);
            goto cleanup;
        }

        reduct_handle_t function = reduct_emit(reduct, node);

        if (shouldDump)
        {
            reduct_dump_function(reduct, function, stdout);
            goto cleanup;
        }

        reduct_handle_t eval = reduct_eval(reduct, function);
        if (isSilent)
        {
            goto cleanup;
        }

        reduct_stringify(reduct, eval, buffer, sizeof(buffer));
        printf("%s", buffer);
        goto cleanup;
    }

    if (!REDUCT_ERROR_SUCCESS(&error))
    {
        fprintf(stderr, "error: %s\n", error.message);
        result = 1;
        goto cleanup;
    }

cleanup:
    reduct_free(reduct);

    return result;
}
