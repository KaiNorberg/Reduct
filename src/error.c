#include "reduct/reduct.h"
#include "reduct/error.h"
#include "reduct/eval.h"
#include "reduct/item.h"

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

static inline reduct_size_t reduct_error_get_region_length(const char* ptr, const char* end)
{
    if (ptr >= end)
    {
        return 0;
    }

    if (*ptr == '(')
    {
        reduct_size_t depth = 0;
        reduct_bool_t inString = REDUCT_FALSE;
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
        return (reduct_size_t)(current - ptr);
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
        return (reduct_size_t)(current - ptr);
    }
    else
    {
        const char* current = ptr;
        while (current < end && *current != ' ' && *current != '\t' && *current != '\n' && *current != '\r' &&
            *current != '(' && *current != ')')
        {
            current++;
        }
        return (reduct_size_t)(current - ptr);
    }
}

static inline void reduct_error_get_row_column_raw(const char* input, reduct_size_t position, reduct_size_t* row,
    reduct_size_t* column)
{
    *row = 1;
    *column = 1;

    if (input == REDUCT_NULL)
    {
        return;
    }

    for (reduct_size_t i = 0; i < position; i++)
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

static inline void reduct_error_print_source_line(reduct_file_t file, const char* input, reduct_size_t inputLength,
    reduct_size_t row, reduct_size_t column, reduct_size_t regionLength, reduct_size_t index)
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

    reduct_size_t lineLen = (reduct_size_t)(lineEnd - lineStart);

    REDUCT_FPRINTF(file, " %4zu | %.*s\n", row, (int)lineLen, lineStart);
    REDUCT_FPRINTF(file, "      | ");

    for (reduct_size_t i = 0; i < column - 1; i++)
    {
        REDUCT_FWRITE(" ", 1, 1, file);
    }
    reduct_size_t indicatorLen = regionLength > 0 ? regionLength : 1;
    reduct_size_t maxIndicatorLen = lineLen > (column - 1) ? lineLen - (column - 1) : 1;
    if (indicatorLen > maxIndicatorLen)
    {
        indicatorLen = maxIndicatorLen;
    }
    for (reduct_size_t i = 0; i < indicatorLen; i++)
    {
        REDUCT_FWRITE("^", 1, 1, file);
    }
    REDUCT_FWRITE("\n", 1, 1, file);
}

static inline void reduct_error_print_context(reduct_file_t file, const char* input, reduct_size_t inputLength,
    reduct_size_t row, reduct_size_t column, reduct_size_t regionLength, reduct_size_t index)
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
            REDUCT_FPRINTF(file, " %4zu | %.*s\n", row - 1, (int)(lineEndPrev - lineStartPrev), lineStartPrev);
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
            REDUCT_FPRINTF(file, " %4zu | %.*s\n", row + 1, (int)(nextEnd - nextLine), nextLine);
        }
    }
}

REDUCT_API void reduct_error_print(reduct_error_t* error, reduct_file_t file)
{
    REDUCT_ASSERT(error != REDUCT_NULL);

    reduct_size_t row;
    reduct_size_t column;
    reduct_error_get_row_column(error, &row, &column);

    const char* typeStr = reduct_error_type_str(error->type);

    if (error->path != REDUCT_NULL)
    {
        REDUCT_FPRINTF(file, "%s:%zu:%zu: %s: %s\n", error->path, row, column, typeStr, error->message);
    }
    else
    {
        REDUCT_FPRINTF(file, "%s: %s\n", typeStr, error->message);
    }

    if (error->input != REDUCT_NULL)
    {
        reduct_error_print_context(file, error->input, error->inputLength, row, column, error->regionLength,
            error->index);
    }
    else
    {
        REDUCT_FWRITE("\n", 1, 1, file);
    }

    if (error->type == REDUCT_ERROR_TYPE_RUNTIME && error->reduct != REDUCT_NULL && error->frameCount > 0)
    {
        REDUCT_FPRINTF(file, "\nStack trace:\n");
        for (reduct_uint8_t i = 0; i < error->frameCount; i++)
        {
            reduct_error_frame_t* f = &error->frames[i];
            const char* fpath = REDUCT_NULL;
            const char* finput = REDUCT_NULL;
            reduct_size_t fpos = f->position;

            if (f->inputId != REDUCT_INPUT_ID_NONE)
            {
                reduct_input_t* fInput = reduct_input_lookup(error->reduct, f->inputId);
                if (fInput != REDUCT_NULL)
                {
                    fpath = fInput->path[0] != '\0' ? fInput->path : "<eval>";
                    finput = fInput->buffer;
                }
            }
            else
            {
                fpath = "<native>";
            }

            reduct_size_t frow = 1, fcol = 1;
            reduct_error_get_row_column_raw(finput, fpos, &frow, &fcol);

            if (fpath != REDUCT_NULL)
            {
                REDUCT_FPRINTF(file, "  at %s:%zu:%zu\n", fpath, frow, fcol);
            }
            else
            {
                REDUCT_FPRINTF(file, "  at <native>\n");
            }
        }
    }
}

REDUCT_API void reduct_error_get_row_column(reduct_error_t* error, reduct_size_t* row, reduct_size_t* column)
{
    REDUCT_ASSERT(error != REDUCT_NULL);
    REDUCT_ASSERT(row != REDUCT_NULL);
    REDUCT_ASSERT(column != REDUCT_NULL);

    reduct_error_get_row_column_raw(error->input, error->index, row, column);
}

REDUCT_API void reduct_error_set(reduct_error_t* error, const char* path, const char* input, reduct_size_t inputLength,
    reduct_size_t regionLength, reduct_size_t position, reduct_error_type_t type, const char* message, ...)
{
    REDUCT_ASSERT(error != REDUCT_NULL);
    REDUCT_ASSERT(message != REDUCT_NULL);

    error->path = path;
    error->input = input;
    error->inputLength = inputLength;
    error->regionLength = regionLength;
    error->type = type;
    error->index = position;
    error->frameCount = 0;

    reduct_va_list args;
    REDUCT_VA_START(args, message);
    REDUCT_VSNPRINTF(error->message, REDUCT_ERROR_MAX_LEN, message, args);
    REDUCT_VA_END(args);

    reduct_size_t wrote = REDUCT_STRLEN(error->message);
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

REDUCT_API void reduct_error_get_item_params(reduct_t* reduct, reduct_item_t* item, const char** path, const char** input,
    reduct_size_t* inputLength, reduct_size_t* regionLength, reduct_size_t* position)
{
    if (item != REDUCT_NULL && item->inputId != REDUCT_INPUT_ID_NONE)
    {
        reduct_input_t* itemInput = reduct_input_lookup(reduct, item->inputId);
        *path = itemInput->path;
        *input = itemInput->buffer;
        *inputLength = (reduct_size_t)(itemInput->end - itemInput->buffer);
        *regionLength = reduct_error_get_region_length(itemInput->buffer + item->position, itemInput->end);
        if (*regionLength == 0)
        {
            *regionLength = 1;
        }

        *position = item->position;
    }
    else
    {
        *path = REDUCT_NULL;
        *input = REDUCT_NULL;
        *inputLength = 0;
        *regionLength = 0;
        *position = 0;
    }
}

REDUCT_API void reduct_error_throw_runtime(struct reduct* reduct, const char* message, ...)
{
    const char* path = REDUCT_NULL;
    const char* input = REDUCT_NULL;
    reduct_size_t inputLength = 0;
    reduct_size_t regionLength = 0;
    reduct_size_t position = 0;

    if (reduct != REDUCT_NULL && reduct->frameCount > 0)
    {
        reduct_eval_frame_t* frame = &reduct->frames[reduct->frameCount - 1];
        if (frame->closure != REDUCT_NULL && frame->closure->function != REDUCT_NULL)
        {
            reduct_function_t* func = frame->closure->function;
            reduct_size_t instIndex = frame->ip > func->insts ? (reduct_size_t)(frame->ip - func->insts - 1) : 0;
            if (instIndex < func->instCount && func->positions != REDUCT_NULL)
            {
                position = func->positions[instIndex];
            }

            reduct_item_t* funcItem = REDUCT_CONTAINER_OF(func, reduct_item_t, function);
            if (funcItem->inputId != REDUCT_INPUT_ID_NONE)
            {
                reduct_input_t* itemInput = reduct_input_lookup(reduct, funcItem->inputId);
                path = itemInput->path;
                input = itemInput->buffer;
                inputLength = (reduct_size_t)(itemInput->end - itemInput->buffer);
                regionLength = reduct_error_get_region_length(input + position, itemInput->end);
                if (regionLength == 0)
                {
                    regionLength = 1;
                }
            }
        }
    }

    reduct_va_list args;
    REDUCT_VA_START(args, message);
    char formattedMessage[REDUCT_ERROR_MAX_LEN];
    REDUCT_VSNPRINTF(formattedMessage, REDUCT_ERROR_MAX_LEN, message, args);
    REDUCT_VA_END(args);

    reduct_error_set(reduct->error, path, input, inputLength, regionLength, position, REDUCT_ERROR_TYPE_RUNTIME, "%s",
        formattedMessage);

    if (reduct != REDUCT_NULL)
    {
        for (reduct_uint32_t i = reduct->frameCount - 1; i > 0; i--)
        {
            if (reduct->error->frameCount >= REDUCT_ERROR_BACKTRACE_MAX)
            {
                break;
            }

            reduct_eval_frame_t* btFrame = &reduct->frames[i - 1];
            if (btFrame->closure == REDUCT_NULL || btFrame->closure->function == REDUCT_NULL)
            {
                continue;
            }

            reduct_function_t* btFunc = btFrame->closure->function;
            reduct_item_t* btFuncItem = REDUCT_CONTAINER_OF(btFunc, reduct_item_t, function);
            reduct_size_t btInstIndex =
                btFrame->ip > btFunc->insts ? (reduct_size_t)(btFrame->ip - btFunc->insts - 1) : 0;
            reduct_uint32_t btPos = (btInstIndex < btFunc->instCount && btFunc->positions != REDUCT_NULL)
                ? btFunc->positions[btInstIndex]
                : 0;

            reduct->error->frames[reduct->error->frameCount].inputId = btFuncItem->inputId;
            reduct->error->frames[reduct->error->frameCount].position = btPos;
            reduct->error->frameCount++;
        }
    }

    REDUCT_LONGJMP(reduct->error->jmp, REDUCT_TRUE);
}
