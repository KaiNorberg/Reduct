#include "reduct/atom.h"
#include "reduct/char.h"
#include "reduct/core.h"
#include "reduct/error.h"
#include "reduct/gc.h"
#include "reduct/item.h"
#include "reduct/list.h"
#include "reduct/parse.h"

typedef struct
{
    reduct_t* reduct;
    const char* ptr;
    reduct_input_t* input;
} reduct_parse_ctx_t;

static inline reduct_list_t* reduct_parse_list(reduct_parse_ctx_t* ctx, char expectedClose, reduct_size_t position);

static void reduct_parse_whitespace(reduct_parse_ctx_t* ctx)
{
    REDUCT_ASSERT(ctx != REDUCT_NULL);

    while (ctx->ptr < ctx->input->end)
    {
        if (REDUCT_CHAR_IS_WHITESPACE(*ctx->ptr))
        {
            ctx->ptr++;
        }
        else if (*ctx->ptr == '/' && ctx->ptr + 1 < ctx->input->end && *(ctx->ptr + 1) == '/')
        {
            while (ctx->ptr < ctx->input->end && *ctx->ptr != '\n')
            {
                ctx->ptr++;
            }
        }
        else if (*ctx->ptr == '/' && ctx->ptr + 1 < ctx->input->end && *(ctx->ptr + 1) == '*')
        {
            ctx->ptr += 2;
            while (ctx->ptr < ctx->input->end)
            {
                if (*ctx->ptr == '*' && ctx->ptr + 1 < ctx->input->end && *(ctx->ptr + 1) == '/')
                {
                    ctx->ptr += 2;
                    break;
                }
                ctx->ptr++;
            }
        }
        else
        {
            break;
        }
    }
}

typedef enum
{
    REDUCT_PARSE_PRECEDENCE_NONE = 0,
    REDUCT_PARSE_PRECEDENCE_LOGICAL_OR,
    REDUCT_PARSE_PRECEDENCE_LOGICAL_AND,
    REDUCT_PARSE_PRECEDENCE_COMPARISON,
    REDUCT_PARSE_PRECEDENCE_BITWISE,
    REDUCT_PARSE_PRECEDENCE_SHIFT,
    REDUCT_PARSE_PRECEDENCE_ADD_SUB,
    REDUCT_PARSE_PRECEDENCE_MUL_DIV,
    REDUCT_PARSE_PRECEDENCE_UNARY,
    REDUCT_PARSE_PRECEDENCE_MAX,
} reduct_parse_precedence_t;

static reduct_bool_t reduct_parse_compare_suffix(reduct_atom_t* atom, const char* suffix)
{
    reduct_size_t suffixLen = REDUCT_STRLEN(suffix);
    if (atom->length < suffixLen)
    {
        return REDUCT_FALSE;
    }
    return REDUCT_MEMCMP(atom->string + atom->length - suffixLen, suffix, suffixLen) == 0;
}

static reduct_parse_precedence_t reduct_parse_get_precedence(reduct_parse_ctx_t* ctx, reduct_handle_t handle, reduct_bool_t unary)
{
    if (!REDUCT_HANDLE_IS_ITEM(&handle))
    {
        return REDUCT_PARSE_PRECEDENCE_NONE;
    }
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(&handle);
    if (item->type != REDUCT_ITEM_TYPE_ATOM)
    {
        return REDUCT_PARSE_PRECEDENCE_NONE;
    }
    reduct_atom_t* atom = &item->atom;

    if (atom->length == 0)
    {
        return REDUCT_PARSE_PRECEDENCE_NONE;
    }

    char last = atom->string[atom->length - 1];

    if (unary)
    {
        if ((last == 't' && reduct_parse_compare_suffix(atom, "not")) ||
            (last == '-' && reduct_parse_compare_suffix(atom, "-")))
        {
            return REDUCT_PARSE_PRECEDENCE_UNARY;
        }
        return REDUCT_PARSE_PRECEDENCE_NONE;
    }

    switch (last)
    {
    case 'r':
        if (reduct_parse_compare_suffix(atom, "or"))
        {
            return REDUCT_PARSE_PRECEDENCE_LOGICAL_OR;
        }
        break;
    case 'd':
        if (reduct_parse_compare_suffix(atom, "and"))
        {
            return REDUCT_PARSE_PRECEDENCE_LOGICAL_AND;
        }
        break;
    case '<':
        if (reduct_parse_compare_suffix(atom, "<<"))
        {
            return REDUCT_PARSE_PRECEDENCE_SHIFT;
        }
        if (reduct_parse_compare_suffix(atom, "<"))
        {
            return REDUCT_PARSE_PRECEDENCE_COMPARISON;
        }
        break;
    case '>':
        if (reduct_parse_compare_suffix(atom, ">>"))
        {
            return REDUCT_PARSE_PRECEDENCE_SHIFT;
        }
        if (reduct_parse_compare_suffix(atom, ">"))
        {
            return REDUCT_PARSE_PRECEDENCE_COMPARISON;
        }
        break;
    case '=':
        if (reduct_parse_compare_suffix(atom, "==") || reduct_parse_compare_suffix(atom, "!=") ||
            reduct_parse_compare_suffix(atom, "<=") || reduct_parse_compare_suffix(atom, ">="))
        {
            return REDUCT_PARSE_PRECEDENCE_COMPARISON;
        }
        break;
    case '&':
    case '|':
    case '^':
        if (reduct_parse_compare_suffix(atom, "&") || reduct_parse_compare_suffix(atom, "|") ||
            reduct_parse_compare_suffix(atom, "^"))
        {
            return REDUCT_PARSE_PRECEDENCE_BITWISE;
        }
        break;
    case '*':
    case '/':
    case '%':
        if (reduct_parse_compare_suffix(atom, "*") || reduct_parse_compare_suffix(atom, "/") ||
            reduct_parse_compare_suffix(atom, "%"))
        {
            return REDUCT_PARSE_PRECEDENCE_MUL_DIV;
        }
        break;
    case '+':
    case '-':
        if (reduct_parse_compare_suffix(atom, "+") || reduct_parse_compare_suffix(atom, "-"))
        {
            return REDUCT_PARSE_PRECEDENCE_ADD_SUB;
        }
        break;
    }

    const char* ptr = ctx->input->buffer + REDUCT_CONTAINER_OF(atom, reduct_item_t, atom)->position;
    REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ptr, "unexpected operator '%.*s'", (int)atom->length, atom->string);
}

static reduct_handle_t reduct_parse_infix_transform_recursive(reduct_parse_ctx_t* ctx, reduct_list_t* list, reduct_size_t* pos,
    reduct_parse_precedence_t minPrecedence)
{
    if (*pos >= list->length)
    {
        return reduct_handle_nil(ctx->reduct);
    }

    reduct_handle_t first = reduct_list_nth(ctx->reduct, list, *pos);
    reduct_handle_t left;
    reduct_parse_precedence_t unaryPrecedence = reduct_parse_get_precedence(ctx, first, REDUCT_TRUE);

    if (unaryPrecedence > REDUCT_PARSE_PRECEDENCE_NONE)
    {
        (*pos)++;
        reduct_handle_t operand = reduct_parse_infix_transform_recursive(ctx, list, pos, unaryPrecedence);
        reduct_list_t* call = reduct_list_new(ctx->reduct);
        reduct_list_push(ctx->reduct, call, first);
        reduct_list_push(ctx->reduct, call, operand);
        left = REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(call, reduct_item_t, list));
    }
    else
    {
        left = first;
        (*pos)++;
    }

    while (*pos < list->length)
    {
        reduct_handle_t op = reduct_list_nth(ctx->reduct, list, *pos);
        reduct_parse_precedence_t prec = reduct_parse_get_precedence(ctx, op, REDUCT_FALSE);
        if (prec == REDUCT_PARSE_PRECEDENCE_NONE || prec < minPrecedence)
        {
            break;
        }

        (*pos)++;
        if (*pos >= list->length)
        {
            reduct_item_t* opItem = REDUCT_HANDLE_TO_ITEM(&op);
            REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input,
                ctx->input->buffer + opItem->position,
                "infix operator '%.*s' missing right operand", (int)opItem->atom.length, opItem->atom.string);
        }

        reduct_handle_t right = reduct_parse_infix_transform_recursive(ctx, list, pos, prec + 1);

        reduct_list_t* call = reduct_list_new(ctx->reduct);
        reduct_list_push(ctx->reduct, call, op);
        reduct_list_push(ctx->reduct, call, left);
        reduct_list_push(ctx->reduct, call, right);
        left = REDUCT_HANDLE_FROM_ITEM(REDUCT_CONTAINER_OF(call, reduct_item_t, list));
    }

    return left;
}

static reduct_list_t* reduct_parse_infix_transform(reduct_parse_ctx_t* ctx, reduct_list_t* list)
{
    if (list->length == 0)
    {
        return reduct_list_new(ctx->reduct);
    }

    reduct_size_t pos = 0;
    reduct_handle_t result = reduct_parse_infix_transform_recursive(ctx, list, &pos, REDUCT_PARSE_PRECEDENCE_NONE);
    return &REDUCT_HANDLE_TO_ITEM(&result)->list;
}

static inline reduct_list_t* reduct_parse_infix(reduct_parse_ctx_t* ctx, reduct_size_t position)
{
    reduct_list_t* list = reduct_parse_list(ctx, '}', position);
    return reduct_parse_infix_transform(ctx, list);
}

static inline reduct_atom_t* reduct_parse_quoted_atom(reduct_parse_ctx_t* ctx)
{
    REDUCT_ASSERT(ctx != REDUCT_NULL);

    const char* start = ctx->ptr;
    while (ctx->ptr < ctx->input->end)
    {
        if (*ctx->ptr == '\\' && ctx->ptr + 1 < ctx->input->end)
        {
            ctx->ptr += 2;
        }
        else if (*ctx->ptr == '"')
        {
            break;
        }
        else
        {
            ctx->ptr++;
        }
    }

    if (ctx->ptr >= ctx->input->end)
    {
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, start, "unexpected end of file, missing '\"'");
    }

    reduct_size_t len = (reduct_size_t)(ctx->ptr - start);
    reduct_atom_t* atom = reduct_atom_lookup(ctx->reduct, start, len, REDUCT_ATOM_LOOKUP_QUOTED);

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    item->inputId = ctx->input->id;
    item->position = (reduct_size_t)(start - ctx->input->buffer);

    if (*ctx->ptr == '"')
    {
        ctx->ptr++;
    }

    return atom;
}

static inline reduct_atom_t* reduct_parse_unquoted_atom(reduct_parse_ctx_t* ctx)
{
    REDUCT_ASSERT(ctx != REDUCT_NULL);

    const char* start = ctx->ptr;
    while (ctx->ptr < ctx->input->end && !REDUCT_CHAR_IS_WHITESPACE(*ctx->ptr) && *ctx->ptr != '(' &&
        *ctx->ptr != ')' && *ctx->ptr != '{' && *ctx->ptr != '}')
    {
        ctx->ptr++;
    }
    reduct_size_t len = (reduct_size_t)(ctx->ptr - start);

    if (len == 0)
    {
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected character '%c'", *ctx->ptr);
    }

    reduct_atom_t* atom = reduct_atom_lookup(ctx->reduct, start, len, REDUCT_ATOM_LOOKUP_NONE);

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    item->inputId = ctx->input->id;
    item->position = (reduct_size_t)(start - ctx->input->buffer);

    return atom;
}

static void reduct_parse_get_in_finalize(reduct_parse_ctx_t* ctx, reduct_list_t* list, reduct_list_t** getInList,
    reduct_handle_t* getInTarget)
{
    if (*getInList == REDUCT_NULL)
    {
        return;
    }

    reduct_list_t* wrapper = reduct_list_new(ctx->reduct);
    reduct_item_t* wrapperItem = REDUCT_CONTAINER_OF(wrapper, reduct_item_t, list);
    reduct_item_t* getInListItem = REDUCT_CONTAINER_OF(*getInList, reduct_item_t, list);
    wrapperItem->inputId = getInListItem->inputId;
    wrapperItem->position = getInListItem->position;

    reduct_list_push(ctx->reduct, wrapper, REDUCT_HANDLE_STRING(ctx->reduct, "get-in"));
    reduct_list_push(ctx->reduct, wrapper, *getInTarget);
    if ((*getInList)->length == 2)
    {
        reduct_list_push(ctx->reduct, wrapper, reduct_list_second(ctx->reduct, *getInList));
    }
    else
    {
        reduct_list_push(ctx->reduct, wrapper, REDUCT_HANDLE_FROM_LIST(*getInList));
    }
    reduct_list_push(ctx->reduct, list, REDUCT_HANDLE_FROM_LIST(wrapper));

    *getInList = REDUCT_NULL;
    *getInTarget = REDUCT_HANDLE_NONE;
}

static void reduct_parse_dot_atom(reduct_parse_ctx_t* ctx, reduct_list_t** getInList, reduct_handle_t* getInTarget,
    reduct_atom_t* atom)
{
    reduct_item_t* atomItem = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    reduct_bool_t isFirst = (*getInList == REDUCT_NULL);

    if (isFirst)
    {
        *getInList = reduct_list_new(ctx->reduct);
        reduct_item_t* getInListItem = REDUCT_CONTAINER_OF(*getInList, reduct_item_t, list);
        getInListItem->inputId = atomItem->inputId;
        getInListItem->position = atomItem->position;

        reduct_list_push(ctx->reduct, *getInList,
            REDUCT_HANDLE_FROM_ATOM(reduct_atom_lookup(ctx->reduct, "list", 4, REDUCT_ATOM_LOOKUP_NONE)));
    }

    const char* dotStart = atom->string;
    const char* dotEnd;
    while ((dotEnd = REDUCT_MEMCHR(dotStart, '.', (reduct_size_t)(atom->string + atom->length - dotStart))) !=
        REDUCT_NULL)
    {
        reduct_size_t partLen = (reduct_size_t)(dotEnd - dotStart);
        if (partLen > 0)
        {
            reduct_atom_lookup_flags_t flags = isFirst ? REDUCT_ATOM_LOOKUP_NONE : REDUCT_ATOM_LOOKUP_QUOTED;
            reduct_atom_t* part = reduct_atom_lookup(ctx->reduct, dotStart, partLen, flags);
            reduct_item_t* partItem = REDUCT_CONTAINER_OF(part, reduct_item_t, atom);
            partItem->inputId = ctx->input->id;
            partItem->position = (reduct_size_t)(dotStart - ctx->input->buffer);

            if (isFirst)
            {
                *getInTarget = REDUCT_HANDLE_FROM_ATOM(part);
                isFirst = REDUCT_FALSE;
            }
            else
            {
                reduct_list_push(ctx->reduct, *getInList, REDUCT_HANDLE_FROM_ATOM(part));
            }
        }
        dotStart = dotEnd + 1;
    }

    reduct_size_t lastLen = (reduct_size_t)(atom->string + atom->length - dotStart);
    if (lastLen > 0)
    {
        reduct_atom_lookup_flags_t flags = isFirst ? REDUCT_ATOM_LOOKUP_NONE : REDUCT_ATOM_LOOKUP_QUOTED;
        reduct_atom_t* lastPart = reduct_atom_lookup(ctx->reduct, dotStart, lastLen, flags);
        reduct_item_t* lastPartItem = REDUCT_CONTAINER_OF(lastPart, reduct_item_t, atom);
        lastPartItem->inputId = ctx->input->id;
        lastPartItem->position = (reduct_size_t)(dotStart - ctx->input->buffer);

        if (isFirst)
        {
            *getInTarget = REDUCT_HANDLE_FROM_ATOM(lastPart);
        }
        else
        {
            reduct_list_push(ctx->reduct, *getInList, REDUCT_HANDLE_FROM_ATOM(lastPart));
        }
    }
}

static inline reduct_list_t* reduct_parse_list(reduct_parse_ctx_t* ctx, char expectedClose, reduct_size_t position)
{
    reduct_list_t* list = reduct_list_new(ctx->reduct);
    reduct_item_t* listItem = REDUCT_CONTAINER_OF(list, reduct_item_t, list);
    listItem->inputId = ctx->input->id;
    listItem->position = position;

    if (ctx->ptr >= ctx->input->end)
    {
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected end of file");
    }

    reduct_handle_t getInTarget = REDUCT_HANDLE_NONE;
    reduct_list_t* getInList = REDUCT_NULL;
    while (1)
    {
        reduct_parse_whitespace(ctx);
        if (ctx->ptr >= ctx->input->end)
        {
            if (expectedClose == '\0')
            {
                break;
            }

            REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected end of file, missing '%c'", expectedClose);
        }

        if (*ctx->ptr == expectedClose)
        {
            ctx->ptr++;
            return list;
        }

        switch (*ctx->ptr)
        {
        case '(':
        {
            reduct_size_t pos = (reduct_size_t)(ctx->ptr - ctx->input->buffer);
            ctx->ptr++;
            reduct_handle_t child = REDUCT_HANDLE_FROM_LIST(reduct_parse_list(ctx, ')', pos));
            reduct_list_push(ctx->reduct, getInList != REDUCT_NULL ? getInList : list, child);
        }
        break;
        case '{':
        {
            reduct_size_t pos = (reduct_size_t)(ctx->ptr - ctx->input->buffer);
            ctx->ptr++;
            reduct_handle_t child = REDUCT_HANDLE_FROM_LIST(reduct_parse_infix(ctx, pos));
            reduct_list_push(ctx->reduct, getInList != REDUCT_NULL ? getInList : list, child);
        }
        break;
        case '"':
        {
            ctx->ptr++;
            reduct_atom_t* atom = reduct_parse_quoted_atom(ctx);
            reduct_list_push(ctx->reduct, getInList != REDUCT_NULL ? getInList : list, REDUCT_HANDLE_FROM_ATOM(atom));
        }
        break;
        default:
        {
            reduct_atom_t* atom = reduct_parse_unquoted_atom(ctx);
            if (!reduct_atom_is_number(atom) && REDUCT_MEMCHR(atom->string, '.', atom->length) != REDUCT_NULL)
            {
                reduct_parse_dot_atom(ctx, &getInList, &getInTarget, atom);
            }
            else
            {
                reduct_list_push(ctx->reduct, getInList != REDUCT_NULL ? getInList : list, REDUCT_HANDLE_FROM_ATOM(atom));
            }
        }
        break;
        }

        if (getInList != REDUCT_NULL && *(ctx->ptr - 1) != '.')
        {
            reduct_parse_get_in_finalize(ctx, list, &getInList, &getInTarget);
        }
    }

    return list;
}

REDUCT_API reduct_handle_t reduct_parse_input(reduct_t* reduct, reduct_input_t* input)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(input != REDUCT_NULL);

    reduct_parse_ctx_t ctx;
    ctx.reduct = reduct;
    ctx.input = input;
    ctx.ptr = input->buffer;

    if (ctx.ptr + 1 < ctx.input->end && *ctx.ptr == '#' && *(ctx.ptr + 1) == '!')
    {
        while (ctx.ptr < ctx.input->end && *ctx.ptr != '\n')
        {
            ctx.ptr++;
        }
    }

    reduct_list_t* list = reduct_parse_list(&ctx, '\0', 0);
    return REDUCT_HANDLE_FROM_LIST(list);
}

REDUCT_API reduct_handle_t reduct_parse(reduct_t* reduct, const char* str, reduct_size_t len, const char* path)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(str != REDUCT_NULL);

    if (reduct == REDUCT_NULL || str == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "invalid arguments");
    }

    reduct_input_t* input = reduct_input_new(reduct, str, len, path, REDUCT_INPUT_FLAG_NONE);
    if (input == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    return reduct_parse_input(reduct, input);
}

REDUCT_API reduct_handle_t reduct_parse_file(reduct_t* reduct, const char* path)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(path != REDUCT_NULL);

    reduct_file_t file = REDUCT_FOPEN(path, "rb");
    if (file == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "could not open file '%s'", path);
    }

    fseek(file, 0, SEEK_END);
    reduct_size_t len = (reduct_size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    if (len == (reduct_size_t)-1)
    {
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_INTERNAL(reduct, "could not read file '%s'", path);
    }

    reduct_size_t allocLen = len == 0 ? 1 : len;
    char* buffer = REDUCT_MALLOC(allocLen);
    if (buffer == REDUCT_NULL)
    {
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    if (len > 0 && REDUCT_FREAD(buffer, 1, len, file) != len)
    {
        REDUCT_FREE(buffer);
        REDUCT_FCLOSE(file);
        REDUCT_ERROR_INTERNAL(reduct, "could not read file '%s'", path);
    }
    REDUCT_FCLOSE(file);

    reduct_input_t* input = reduct_input_new(reduct, buffer, len, path, REDUCT_INPUT_FLAG_OWNED);
    if (input == REDUCT_NULL)
    {
        REDUCT_FREE(buffer);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    return reduct_parse_input(reduct, input);
}
