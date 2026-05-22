#include "reduct/parse.h"
#include "reduct/atom.h"
#include "reduct/char.h"
#include "reduct/core.h"
#include "reduct/error.h"
#include "reduct/gc.h"
#include "reduct/item.h"
#include "reduct/list.h"

typedef struct
{
    reduct_t* reduct;
    const char* ptr;
    reduct_input_t* input;
} reduct_parse_ctx_t;

static reduct_handle_t reduct_parse_expression(reduct_parse_ctx_t* ctx);
static inline reduct_list_t* reduct_parse_list(reduct_parse_ctx_t* ctx, char expectedClose, size_t position);
static inline reduct_atom_t* reduct_parse_quoted_atom(reduct_parse_ctx_t* ctx);
static inline reduct_atom_t* reduct_parse_unquoted_atom(reduct_parse_ctx_t* ctx);
static inline reduct_list_t* reduct_parse_infix(reduct_parse_ctx_t* ctx, size_t position);

static void reduct_parse_whitespace(reduct_parse_ctx_t* ctx)
{
    assert(ctx != NULL);

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

static reduct_handle_t reduct_parse_dot_finalize(reduct_parse_ctx_t* ctx, reduct_handle_t target, reduct_list_t* path)
{
    if (REDUCT_HANDLE_IS_NIL(target))
    {
        reduct_item_t* pathItem = REDUCT_CONTAINER_OF(path, reduct_item_t, list);
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->input->buffer + pathItem->position,
            "dot notation: missing target");
    }

    reduct_list_t* wrapper = reduct_list_new(ctx->reduct);
    reduct_item_t* wrapperItem = REDUCT_CONTAINER_OF(wrapper, reduct_item_t, list);
    reduct_item_t* pathItem = REDUCT_CONTAINER_OF(path, reduct_item_t, list);
    wrapperItem->inputId = pathItem->inputId;
    wrapperItem->position = pathItem->position;

    reduct_list_push(ctx->reduct, wrapper, REDUCT_HANDLE_CREATE_SYMBOL(ctx->reduct, "get-in"));
    reduct_list_push(ctx->reduct, wrapper, target);

    if (path->length == 2)
    {
        reduct_list_push(ctx->reduct, wrapper, reduct_list_second(ctx->reduct, path));
    }
    else
    {
        reduct_list_push(ctx->reduct, wrapper, REDUCT_HANDLE_FROM_LIST(path));
    }

    return REDUCT_HANDLE_FROM_LIST(wrapper);
}

static reduct_handle_t reduct_parse_dot_chain(reduct_parse_ctx_t* ctx, reduct_handle_t head)
{
    reduct_handle_t target = REDUCT_HANDLE_NIL(ctx->reduct);
    reduct_list_t* path = reduct_list_new(ctx->reduct);
    reduct_list_push(ctx->reduct, path, REDUCT_HANDLE_CREATE_SYMBOL(ctx->reduct, "list"));

    bool isFirst = true;
    reduct_handle_t current = head;
    while (1)
    {
        if (!REDUCT_HANDLE_IS_ATOM(current))
        {
            reduct_list_push(ctx->reduct, path, current);
            break;
        }

        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(current);
        const char* start = atom->string;
        const char* dot;

        while ((dot = memchr(start, '.', (size_t)(atom->string + atom->length - start))) != NULL)
        {
            size_t len = (size_t)(dot - start);
            if (len > 0)
            {
                reduct_atom_t* part = reduct_atom_lookup(ctx->reduct, start, len,
                    isFirst ? REDUCT_ATOM_LOOKUP_NONE : REDUCT_ATOM_LOOKUP_QUOTED);
                if (isFirst)
                {
                    target = REDUCT_HANDLE_FROM_ATOM(part);
                    isFirst = false;
                }
                else
                {
                    reduct_list_push(ctx->reduct, path, REDUCT_HANDLE_FROM_ATOM(part));
                }
            }
            start = dot + 1;
        }

        size_t remaining = (size_t)(atom->string + atom->length - start);
        if (remaining > 0)
        {
            reduct_atom_t* part = reduct_atom_lookup(ctx->reduct, start, remaining,
                isFirst ? REDUCT_ATOM_LOOKUP_NONE : REDUCT_ATOM_LOOKUP_QUOTED);
            if (isFirst)
            {
                target = REDUCT_HANDLE_FROM_ATOM(part);
                isFirst = false;
            }
            else
            {

                reduct_list_push(ctx->reduct, path, REDUCT_HANDLE_FROM_ATOM(part));
            }
        }

        if (atom->length > 0 && atom->string[atom->length - 1] == '.')
        {
            reduct_parse_whitespace(ctx);
            if (ctx->ptr >= ctx->input->end)
            {
                break;
            }
            current = reduct_parse_expression(ctx);
            continue;
        }
        break;
    }

    return reduct_parse_dot_finalize(ctx, target, path);
}

static reduct_handle_t reduct_parse_expression(reduct_parse_ctx_t* ctx)
{
    size_t pos = (size_t)(ctx->ptr - ctx->input->buffer);
    reduct_handle_t result;

    switch (*ctx->ptr)
    {
    case '(':
        ctx->ptr++;
        return REDUCT_HANDLE_FROM_LIST(reduct_parse_list(ctx, ')', pos));
    case '{':
        ctx->ptr++;
        return REDUCT_HANDLE_FROM_LIST(reduct_parse_infix(ctx, pos));
    case '"':
        ctx->ptr++;
        return REDUCT_HANDLE_FROM_ATOM(reduct_parse_quoted_atom(ctx));
    default:
        result = REDUCT_HANDLE_FROM_ATOM(reduct_parse_unquoted_atom(ctx));
        break;
    }

    if (REDUCT_HANDLE_IS_ATOM(result))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(result);
        if (!reduct_atom_is_number(atom) && memchr(atom->string, '.', atom->length))
        {
            return reduct_parse_dot_chain(ctx, result);
        }
    }

    return result;
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

static bool reduct_parse_compare_suffix(reduct_atom_t* atom, const char* suffix)
{
    size_t suffixLen = strlen(suffix);
    if (atom->length < suffixLen)
    {
        return false;
    }
    return memcmp(atom->string + atom->length - suffixLen, suffix, suffixLen) == 0;
}

static reduct_atom_t* reduct_parse_get_operator_atom(reduct_parse_ctx_t* ctx, reduct_handle_t handle, bool unary)
{
    if (!REDUCT_HANDLE_IS_ITEM(handle))
    {
        return NULL;
    }
    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);

    if (item->type == REDUCT_ITEM_TYPE_ATOM)
    {
        return &item->atom;
    }
    else if (!unary && item->type == REDUCT_ITEM_TYPE_LIST && item->length >= 3)
    {
        reduct_list_t* list = &item->list;
        reduct_handle_t head = reduct_list_first(ctx->reduct, list);
        if (REDUCT_HANDLE_IS_ATOM(head) && reduct_atom_is_equal(REDUCT_HANDLE_TO_ATOM(head), "get-in", 6))
        {
            reduct_handle_t last = reduct_list_nth(ctx->reduct, list, list->length - 1);
            if (REDUCT_HANDLE_IS_ATOM(last))
            {
                return REDUCT_HANDLE_TO_ATOM(last);
            }
        }
    }
    return NULL;
}

static reduct_parse_precedence_t reduct_parse_get_precedence(reduct_parse_ctx_t* ctx, reduct_handle_t handle,
    bool unary)
{
    reduct_atom_t* atom = reduct_parse_get_operator_atom(ctx, handle, unary);

    if (atom == NULL)
    {
        return REDUCT_PARSE_PRECEDENCE_NONE;
    }

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
    REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ptr, "unexpected operator '%.*s'", (int)atom->length,
        atom->string);
}

static reduct_handle_t reduct_parse_infix_transform_recursive(reduct_parse_ctx_t* ctx, reduct_list_t* list, size_t* pos,
    reduct_parse_precedence_t minPrecedence)
{
    if (*pos >= list->length)
    {
        return REDUCT_HANDLE_NIL(ctx->reduct);
    }

    reduct_handle_t first = reduct_list_nth(ctx->reduct, list, *pos);
    reduct_handle_t left;
    reduct_parse_precedence_t unaryPrecedence = reduct_parse_get_precedence(ctx, first, true);

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
        reduct_parse_precedence_t prec = reduct_parse_get_precedence(ctx, op, false);
        if (prec == REDUCT_PARSE_PRECEDENCE_NONE || prec < minPrecedence)
        {
            break;
        }

        (*pos)++;
        if (*pos >= list->length)
        {
            reduct_atom_t* opAtom = reduct_parse_get_operator_atom(ctx, op, false);
            reduct_item_t* opItem = REDUCT_HANDLE_TO_ITEM(op);
            REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->input->buffer + opItem->position,
                "infix operator '%.*s' missing right operand", (int)opAtom->length, opAtom->string);
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

    size_t pos = 0;
    reduct_handle_t result = reduct_parse_infix_transform_recursive(ctx, list, &pos, REDUCT_PARSE_PRECEDENCE_NONE);
    return REDUCT_HANDLE_TO_LIST(result);
}

static inline reduct_list_t* reduct_parse_infix(reduct_parse_ctx_t* ctx, size_t position)
{
    reduct_list_t* list = reduct_parse_list(ctx, '}', position);
    return reduct_parse_infix_transform(ctx, list);
}

static inline reduct_atom_t* reduct_parse_quoted_atom(reduct_parse_ctx_t* ctx)
{
    assert(ctx != NULL);

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

    size_t len = (size_t)(ctx->ptr - start);
    reduct_atom_t* atom = reduct_atom_lookup(ctx->reduct, start, len, REDUCT_ATOM_LOOKUP_QUOTED);

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    item->inputId = ctx->input->id;
    item->position = (size_t)(start - ctx->input->buffer);

    if (*ctx->ptr == '"')
    {
        ctx->ptr++;
    }

    return atom;
}

static inline reduct_atom_t* reduct_parse_unquoted_atom(reduct_parse_ctx_t* ctx)
{
    assert(ctx != NULL);

    const char* start = ctx->ptr;
    while (ctx->ptr < ctx->input->end && !REDUCT_CHAR_IS_WHITESPACE(*ctx->ptr) && *ctx->ptr != '(' &&
        *ctx->ptr != ')' && *ctx->ptr != '{' && *ctx->ptr != '}')
    {
        ctx->ptr++;
    }
    size_t len = (size_t)(ctx->ptr - start);

    if (len == 0)
    {
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected character '%c'", *ctx->ptr);
    }

    reduct_atom_t* atom = reduct_atom_lookup(ctx->reduct, start, len, REDUCT_ATOM_LOOKUP_NONE);

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    item->inputId = ctx->input->id;
    item->position = (size_t)(start - ctx->input->buffer);

    return atom;
}

static inline reduct_list_t* reduct_parse_list(reduct_parse_ctx_t* ctx, char expectedClose, size_t position)
{
    reduct_list_t* list = reduct_list_new(ctx->reduct);
    reduct_item_t* listItem = REDUCT_CONTAINER_OF(list, reduct_item_t, list);
    listItem->inputId = ctx->input->id;
    listItem->position = position;

    if (ctx->ptr >= ctx->input->end)
    {
        REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected end of file");
    }

    while (1)
    {
        reduct_parse_whitespace(ctx);
        if (ctx->ptr >= ctx->input->end)
        {
            if (expectedClose == '\0')
            {
                break;
            }

            REDUCT_ERROR_SYNTAX(ctx->reduct->error, ctx->input, ctx->ptr, "unexpected end of file, missing '%c'",
                expectedClose);
        }

        if (*ctx->ptr == expectedClose)
        {
            ctx->ptr++;
            return list;
        }

        reduct_list_push(ctx->reduct, list, reduct_parse_expression(ctx));
    }

    return list;
}

REDUCT_API reduct_handle_t reduct_parse_input(reduct_t* reduct, reduct_input_t* input)
{
    assert(reduct != NULL);
    assert(input != NULL);

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

    if (list->length == 0)
    {
        return REDUCT_HANDLE_NIL(reduct);
    }

    if (list->length == 1)
    {
        return reduct_list_first(reduct, list);
    }

    reduct_list_t* wrapper = reduct_list_new(reduct);
    reduct_item_t* wrapperItem = REDUCT_CONTAINER_OF(wrapper, reduct_item_t, list);
    wrapperItem->inputId = input->id;
    wrapperItem->position = 0;

    reduct_list_push(reduct, wrapper, REDUCT_HANDLE_CREATE_SYMBOL(reduct, "do"));
    reduct_list_push_list(reduct, wrapper, list);

    return REDUCT_HANDLE_FROM_LIST(wrapper);
}

REDUCT_API reduct_handle_t reduct_parse(reduct_t* reduct, const char* str, size_t len, const char* path)
{
    assert(reduct != NULL);
    assert(str != NULL);

    if (reduct == NULL || str == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "invalid arguments");
    }

    reduct_input_t* input = reduct_input_new(reduct, str, len, path, REDUCT_INPUT_FLAG_NONE);
    if (input == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    return reduct_parse_input(reduct, input);
}

REDUCT_API reduct_handle_t reduct_parse_file(reduct_t* reduct, const char* path)
{
    assert(reduct != NULL);
    assert(path != NULL);

    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "could not open file '%s'", path);
    }

    fseek(file, 0, SEEK_END);
    size_t len = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    if (len == (size_t)-1)
    {
        fclose(file);
        REDUCT_ERROR_INTERNAL(reduct, "could not read file '%s'", path);
    }

    size_t allocLen = len == 0 ? 1 : len;
    char* buffer = malloc(allocLen);
    if (buffer == NULL)
    {
        fclose(file);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    if (len > 0 && fread(buffer, 1, len, file) != len)
    {
        free(buffer);
        fclose(file);
        REDUCT_ERROR_INTERNAL(reduct, "could not read file '%s'", path);
    }
    fclose(file);

    reduct_input_t* input = reduct_input_new(reduct, buffer, len, path, REDUCT_INPUT_FLAG_OWNED);
    if (input == NULL)
    {
        free(buffer);
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    return reduct_parse_input(reduct, input);
}
