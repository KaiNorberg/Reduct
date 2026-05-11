#include "reduct/error.h"
#include "reduct/eval.h"
#include "reduct/item.h"
#include "reduct/reduct.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline const char* reduct_error_type_str(reduct_error_type_t type)
{
    switch (type)
    {
    case REDUCT_ERROR_TYPE_SYNTAX:
        return "syntax error";
    case REDUCT_ERROR_TYPE_COMPILE:
        return "compile error";
    case REDUCT_ERROR_TYPE_RUNTIME:
        return "runtime error";
    case REDUCT_ERROR_TYPE_INTERNAL:
        return "internal error";
    default:
        return "error";
    }
}

static inline size_t reduct_error_get_region_length(const char* ptr, const char* end)
{
    if (ptr >= end)
    {
        return 0;
    }

    if (*ptr == '(')
    {
        size_t depth = 0;
        bool inString = false;
        const char* current = ptr;
        while (current < end)
        {
            if (*current == '\\' && current + 1 < end)
            {
                current += 2;
                continue;
            }
            if (*current == '"')
            {
                inString = !inString;
            }
            else if (!inString)
            {
                if (*current == '(')
                {
                    depth++;
                }
                else if (*current == ')')
                {
                    depth--;
                    if (depth == 0)
                    {
                        current++;
                        break;
                    }
                }
            }
            current++;
        }
        return (size_t)(current - ptr);
    }
    else if (*ptr == '"')
    {
        const char* current = ptr + 1;
        while (current < end)
        {
            if (*current == '\\' && current + 1 < end)
            {
                current += 2;
                continue;
            }
            if (*current == '"')
            {
                current++;
                break;
            }
            current++;
        }
        return (size_t)(current - ptr);
    }
    else
    {
        const char* current = ptr;
        while (current < end && *current != ' ' && *current != '\t' && *current != '\n' && *current != '\r' &&
            *current != '(' && *current != ')')
        {
            current++;
        }
        return (size_t)(current - ptr);
    }
}

static inline void reduct_error_get_row_column_raw(const char* input, size_t position, size_t* row, size_t* column)
{
    *row = 1;
    *column = 1;

    if (input == NULL)
    {
        return;
    }

    for (size_t i = 0; i < position; i++)
    {
        if (input[i] == '\n')
        {
            (*row)++;
            *column = 1;
        }
        else
        {
            (*column)++;
        }
    }
}

static inline void reduct_error_print_source_line(FILE* file, const char* input, size_t inputLength, size_t row,
    size_t column, size_t regionLength, size_t index)
{
    const char* lineStart = input + index;
    while (lineStart > input && *(lineStart - 1) != '\n')
    {
        lineStart--;
    }

    const char* lineEnd = input + index;
    while (lineEnd < input + inputLength && *lineEnd != '\n' && *lineEnd != '\r')
    {
        lineEnd++;
    }

    size_t lineLen = (size_t)(lineEnd - lineStart);

    fprintf(file, " %4zu | %.*s\n", row, (int)lineLen, lineStart);
    fprintf(file, "      | ");

    for (size_t i = 0; i < column - 1; i++)
    {
        fwrite(" ", 1, 1, file);
    }
    size_t indicatorLen = regionLength > 0 ? regionLength : 1;
    size_t maxIndicatorLen = lineLen > (column - 1) ? lineLen - (column - 1) : 1;
    if (indicatorLen > maxIndicatorLen)
    {
        indicatorLen = maxIndicatorLen;
    }
    for (size_t i = 0; i < indicatorLen; i++)
    {
        fwrite("^", 1, 1, file);
    }
    fwrite("\n", 1, 1, file);
}

static inline void reduct_error_print_context(FILE* file, const char* input, size_t inputLength, size_t row,
    size_t column, size_t regionLength, size_t index)
{
    if (row > 1)
    {
        const char* lineStartCurr = input + index;
        while (lineStartCurr > input && *(lineStartCurr - 1) != '\n')
        {
            lineStartCurr--;
        }
        if (lineStartCurr > input)
        {
            const char* lineStartPrev = lineStartCurr - 1;
            while (lineStartPrev > input && *(lineStartPrev - 1) != '\n')
            {
                lineStartPrev--;
            }
            const char* lineEndPrev = lineStartCurr - 1;
            while (lineEndPrev < input + inputLength && *lineEndPrev != '\n' && *lineEndPrev != '\r')
            {
                lineEndPrev++;
            }
            fprintf(file, " %4zu | %.*s\n", row - 1, (int)(lineEndPrev - lineStartPrev), lineStartPrev);
        }
    }

    reduct_error_print_source_line(file, input, inputLength, row, column, regionLength, index);

    const char* lineEndCurr = input + index;
    while (lineEndCurr < input + inputLength && *lineEndCurr != '\n' && *lineEndCurr != '\r')
    {
        lineEndCurr++;
    }

    if (lineEndCurr < input + inputLength && (*lineEndCurr == '\n' || *lineEndCurr == '\r'))
    {
        const char* nextLine = lineEndCurr + 1;
        if (nextLine < input + inputLength)
        {
            const char* nextEnd = nextLine;
            while (nextEnd < input + inputLength && *nextEnd != '\n' && *nextEnd != '\r')
            {
                nextEnd++;
            }
            fprintf(file, " %4zu | %.*s\n", row + 1, (int)(nextEnd - nextLine), nextLine);
        }
    }
}

REDUCT_API void reduct_error_print(reduct_error_t* error, FILE* file)
{
    assert(error != NULL);

    size_t row;
    size_t column;
    reduct_error_get_row_column(error, &row, &column);

    const char* typeStr = reduct_error_type_str(error->type);

    if (error->path != NULL)
    {
        fprintf(file, "%s:%zu:%zu: %s: %s\n", error->path, row, column, typeStr, error->message);
    }
    else
    {
        fprintf(file, "%s: %s\n", typeStr, error->message);
    }

    if (error->input != NULL)
    {
        reduct_error_print_context(file, error->input, error->inputLength, row, column, error->regionLength,
            error->index);
    }
    else
    {
        fwrite("\n", 1, 1, file);
    }

    if (error->type == REDUCT_ERROR_TYPE_RUNTIME && error->reduct != NULL && error->frameCount > 0)
    {
        fprintf(file, "\nStack trace:\n");
        for (uint8_t i = 0; i < error->frameCount; i++)
        {
            reduct_error_frame_t* f = &error->frames[i];
            const char* fpath = NULL;
            const char* finput = NULL;
            size_t fpos = f->position;

            if (f->inputId != REDUCT_INPUT_ID_NONE)
            {
                reduct_input_t* fInput = reduct_input_lookup(error->reduct, f->inputId);
                if (fInput != NULL)
                {
                    fpath = fInput->path[0] != '\0' ? fInput->path : "<eval>";
                    finput = fInput->buffer;
                }
            }
            else
            {
                fpath = "<native>";
            }

            size_t frow = 1, fcol = 1;
            reduct_error_get_row_column_raw(finput, fpos, &frow, &fcol);

            if (fpath != NULL)
            {
                fprintf(file, "  at %s:%zu:%zu\n", fpath, frow, fcol);
            }
            else
            {
                fprintf(file, "  at <native>\n");
            }
        }
    }
}

REDUCT_API void reduct_error_get_row_column(reduct_error_t* error, size_t* row, size_t* column)
{
    assert(error != NULL);
    assert(row != NULL);
    assert(column != NULL);

    reduct_error_get_row_column_raw(error->input, error->index, row, column);
}

REDUCT_API void reduct_error_set(reduct_error_t* error, const char* path, const char* input, size_t inputLength,
    size_t regionLength, size_t position, reduct_error_type_t type, const char* message, ...)
{
    assert(error != NULL);
    assert(message != NULL);

    error->path = path;
    error->input = input;
    error->inputLength = inputLength;
    error->regionLength = regionLength;
    error->type = type;
    error->index = position;
    error->frameCount = 0;

    va_list args;
    va_start(args, message);
    vsnprintf(error->message, REDUCT_ERROR_MAX_LEN, message, args);
    va_end(args);

    size_t wrote = strlen(error->message);
    if (wrote == REDUCT_ERROR_MAX_LEN - 1)
    {
        if (REDUCT_ERROR_MAX_LEN >= 4)
        {
            error->message[REDUCT_ERROR_MAX_LEN - 4] = '.';
            error->message[REDUCT_ERROR_MAX_LEN - 3] = '.';
            error->message[REDUCT_ERROR_MAX_LEN - 2] = '.';
            error->message[REDUCT_ERROR_MAX_LEN - 1] = '\0';
        }
    }
}

REDUCT_API void reduct_error_get_item_params(reduct_t* reduct, reduct_item_t* item, const char** path,
    const char** input, size_t* inputLength, size_t* regionLength, size_t* position)
{
    if (item != NULL && item->inputId != REDUCT_INPUT_ID_NONE)
    {
        reduct_input_t* itemInput = reduct_input_lookup(reduct, item->inputId);
        *path = itemInput->path;
        *input = itemInput->buffer;
        *inputLength = (size_t)(itemInput->end - itemInput->buffer);
        *regionLength = reduct_error_get_region_length(itemInput->buffer + item->position, itemInput->end);
        if (*regionLength == 0)
        {
            *regionLength = 1;
        }

        *position = item->position;
    }
    else
    {
        *path = NULL;
        *input = NULL;
        *inputLength = 0;
        *regionLength = 0;
        *position = 0;
    }
}

REDUCT_API void reduct_error_throw_runtime(struct reduct* reduct, const char* message, ...)
{
    const char* path = NULL;
    const char* input = NULL;
    size_t inputLength = 0;
    size_t regionLength = 0;
    size_t position = 0;

    if (reduct != NULL && reduct->frameCount > 0)
    {
        reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount - 1];
        if (frame->closure != NULL && frame->closure->function != NULL)
        {
            reduct_function_t* func = frame->closure->function;
            size_t instIndex = frame->ip > func->insts ? (size_t)(frame->ip - func->insts - 1) : 0;
            if (instIndex < func->instCount && func->positions != NULL)
            {
                position = func->positions[instIndex];
            }

            reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);
            if (funcItem->inputId != REDUCT_INPUT_ID_NONE)
            {
                reduct_input_t* itemInput = reduct_input_lookup(reduct, funcItem->inputId);
                path = itemInput->path;
                input = itemInput->buffer;
                inputLength = (size_t)(itemInput->end - itemInput->buffer);
                regionLength = reduct_error_get_region_length(input + position, itemInput->end);
                if (regionLength == 0)
                {
                    regionLength = 1;
                }
            }
        }
    }

    va_list args;
    va_start(args, message);
    char formattedMessage[REDUCT_ERROR_MAX_LEN];
    vsnprintf(formattedMessage, REDUCT_ERROR_MAX_LEN, message, args);
    va_end(args);

    reduct_error_set(reduct->error, path, input, inputLength, regionLength, position, REDUCT_ERROR_TYPE_RUNTIME, "%s",
        formattedMessage);

    if (reduct != NULL)
    {
        for (uint32_t i = reduct->frameCount - 1; i > 0; i--)
        {
            if (reduct->error->frameCount >= REDUCT_ERROR_BACKTRACE_MAX)
            {
                break;
            }

            reduct_eval_frame_t* btFrame = &reduct->frames[i - 1];
            if (btFrame->closure == NULL || btFrame->closure->function == NULL)
            {
                continue;
            }

            reduct_function_t* btFunc = btFrame->closure->function;
            reduct_item_t* btFuncItem = REDUCT_CONTAINER_OF(btFunc, reduct_item_t, function);
            size_t btInstIndex = btFrame->ip > btFunc->insts ? (size_t)(btFrame->ip - btFunc->insts - 1) : 0;
            uint32_t btPos =
                (btInstIndex < btFunc->instCount && btFunc->positions != NULL) ? btFunc->positions[btInstIndex] : 0;

            reduct->error->frames[reduct->error->frameCount].inputId = btFuncItem->inputId;
            reduct->error->frames[reduct->error->frameCount].position = btPos;
            reduct->error->frameCount++;
        }
    }

    longjmp(reduct->error->jmp, true);
}
