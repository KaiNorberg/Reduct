#include "reduct/sync.h"
#include <reduct/atom.h>
#include <reduct/char.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/gc.h>
#include <reduct/item.h>

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

REDUCT_API void reduct_atom_global_init(reduct_atom_global_t* global)
{
    assert(global != NULL);
    global->map = NULL;
    global->size = 0;
    global->capacity = 0;
    global->mask = 0;
    global->tombstones = 0;
    reduct_rwmutex_init(&global->mutex);
}

REDUCT_API void reduct_atom_global_deinit(reduct_atom_global_t* global)
{
    assert(global != NULL);
    if (global->map != NULL)
    {
        free(global->map);
        global->map = NULL;
    }
    global->size = 0;
    global->capacity = 0;
    global->mask = 0;
    global->tombstones = 0;
    reduct_rwmutex_destroy(&global->mutex);
}

REDUCT_API void reduct_atom_local_init(reduct_atom_local_t* local)
{
    assert(local != NULL);
    local->atomStack = NULL;
}

REDUCT_API void reduct_atom_local_deinit(reduct_atom_local_t* local)
{
    assert(local != NULL);
    local->atomStack = NULL;
}

static inline bool reduct_atom_map_is_alive(reduct_atom_t* slot)
{
    return slot != NULL && slot != REDUCT_ATOM_TOMBSTONE;
}

static inline size_t reduct_atom_map_find(reduct_t* reduct, uint32_t hash, const char* str, size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);
    assert(reduct->global->atom.capacity != 0);
    assert((reduct->global->atom.capacity & (reduct->global->atom.capacity - 1)) == 0);
    assert(reduct->global->atom.size <= reduct->global->atom.capacity);

    bool wantQuoted = (flags & REDUCT_ATOM_LOOKUP_QUOTED) != 0;

    size_t index = hash & reduct->global->atom.mask;
    size_t step = 1;

    size_t tombstoneIndex = REDUCT_ATOM_INDEX_NONE;

    while (reduct->global->atom.map[index] != NULL)
    {
        reduct_atom_t* atom = reduct->global->atom.map[index];

        if (atom == REDUCT_ATOM_TOMBSTONE)
        {
            if (tombstoneIndex == REDUCT_ATOM_INDEX_NONE)
            {
                tombstoneIndex = index;
            }
        }
        else
        {
            bool isQuoted = (atom->flags & REDUCT_ATOM_FLAG_QUOTED) != 0;

            if (atom->hash == hash && isQuoted == wantQuoted && reduct_atom_is_equal(atom, str, len))
            {
                return index;
            }
        }

        index = (index + step) & reduct->global->atom.mask;
        step++;
    }

    if (tombstoneIndex != REDUCT_ATOM_INDEX_NONE)
    {
        return tombstoneIndex;
    }

    return index;
}

static inline size_t reduct_atom_map_find_or_alloc(reduct_t* reduct, uint32_t hash, const char* str, size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);

    if (reduct->global->atom.capacity == 0)
    {
        reduct->global->atom.capacity = REDUCT_ATOM_MAP_INITIAL;
        reduct->global->atom.mask = reduct->global->atom.capacity - 1;
        reduct->global->atom.tombstones = 0;
        reduct->global->atom.map = calloc(reduct->global->atom.capacity, sizeof(reduct_atom_t*));
        if (reduct->global->atom.map == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    size_t occupied = reduct->global->atom.size + reduct->global->atom.tombstones;
    if (occupied * 4 > reduct->global->atom.capacity * 3)
    {
        size_t oldCapacity = reduct->global->atom.capacity;
        reduct_atom_t** oldMap = reduct->global->atom.map;

        reduct->global->atom.capacity = oldCapacity * REDUCT_ATOM_MAP_GROWTH;
        reduct->global->atom.mask = reduct->global->atom.capacity - 1;
        reduct->global->atom.tombstones = 0;
        reduct->global->atom.map = calloc(reduct->global->atom.capacity, sizeof(reduct_atom_t*));
        if (reduct->global->atom.map == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }

        for (size_t i = 0; i < oldCapacity; i++)
        {
            reduct_atom_t* atom = oldMap[i];

            if (!reduct_atom_map_is_alive(atom))
            {
                continue;
            }

            size_t index = atom->hash & reduct->global->atom.mask;
            size_t step = 1;
            while (reduct->global->atom.map[index] != NULL)
            {
                index = (index + step) & reduct->global->atom.mask;
                step++;
            }

            reduct->global->atom.map[index] = atom;
            atom->index = (uint32_t)index;
        }

        if (oldMap != NULL)
        {
            free(oldMap);
        }
    }

    size_t index = reduct_atom_map_find(reduct, hash, str, len, flags);

    if (reduct_atom_map_is_alive(reduct->global->atom.map[index]))
    {
        return index;
    }

    if (reduct->global->atom.map[index] == REDUCT_ATOM_TOMBSTONE)
    {
        reduct->global->atom.tombstones--;
    }

    reduct->global->atom.size++;
    return index;
}

static inline void reduct_atom_map_update(reduct_t* reduct, reduct_atom_t* atom, size_t newIndex, uint32_t hash)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (atom->index != REDUCT_ATOM_INDEX_NONE)
    {
        reduct->global->atom.map[atom->index] = REDUCT_ATOM_TOMBSTONE;
        reduct->global->atom.tombstones++;
    }

    reduct->global->atom.map[newIndex] = atom;
    atom->index = (uint32_t)newIndex;
    atom->hash = hash;
}

REDUCT_API bool reduct_atom_is_equal(reduct_atom_t* atom, const char* str, size_t len)
{
    if (atom->length != len)
    {
        return false;
    }

    return memcmp(atom->string, str, len) == 0;
}

static inline reduct_atom_stack_t* reduct_atom_stack_new(reduct_t* reduct, size_t capacity)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_ATOM_STACK;

    reduct_atom_stack_t* stack = &item->atomStack;
    stack->capacity = (uint32_t)capacity;
    stack->count = 0;
    stack->data = malloc(capacity);
    if (stack->data == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    stack->next = reduct->atom.atomStack;
    stack->prev = NULL;
    if (reduct->atom.atomStack != NULL)
    {
        reduct->atom.atomStack->prev = stack;
    }
    reduct->atom.atomStack = stack;
    return stack;
}

static inline char* reduct_atom_stack_alloc(reduct_t* reduct, size_t size, reduct_atom_stack_t** out)
{
    assert(reduct != NULL);

    reduct_atom_stack_t* stack = reduct->atom.atomStack;
    if (stack == NULL || stack->count + size > stack->capacity)
    {
        size_t capacity = REDUCT_ATOM_STACK_MIN;
        while (capacity < size)
        {
            capacity *= REDUCT_ATOM_MAP_GROWTH;
        }
        stack = reduct_atom_stack_new(reduct, capacity);
        if (stack == NULL)
        {
            return NULL;
        }
    }

    char* data = stack->data + stack->count;
    stack->count += (uint32_t)size;
    if (out != NULL)
    {
        *out = stack;
    }
    return data;
}

REDUCT_API reduct_atom_t* reduct_atom_new(reduct_t* reduct, size_t len)
{
    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_ATOM;

    reduct_atom_t* atom = &item->atom;
    atom->hash = 0;
    atom->index = REDUCT_ATOM_INDEX_NONE;
    atom->flags = 0;
    atom->string = NULL;
    atom->length = (uint32_t)len;

    if (len <= REDUCT_ATOM_SMALL_MAX)
    {
        atom->string = atom->smallString;
    }
    else
    {
        atom->string = reduct_atom_stack_alloc(reduct, len, &atom->stack);
        if (atom->string == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        atom->flags |= REDUCT_ATOM_FLAG_LARGE;
    }

    return atom;
}

REDUCT_API reduct_atom_t* reduct_atom_new_string(struct reduct* reduct, const char* str)
{
    assert(reduct != NULL);
    assert(str != NULL);

    return reduct_atom_new_copy(reduct, str, strlen(str));
}

REDUCT_API reduct_atom_t* reduct_atom_new_number(reduct_t* reduct, double value)
{
    assert(reduct != NULL);

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.17g", value);

    reduct_atom_t* atom = reduct_atom_new(reduct, (size_t)len);
    memcpy(atom->string, buf, (size_t)len);
    atom->numberValue = value;
    atom->flags |= REDUCT_ATOM_FLAG_NUMBER | REDUCT_ATOM_FLAG_NUMBER_CHECKED;
    return atom;
}

REDUCT_API reduct_atom_t* reduct_atom_new_native(struct reduct* reduct, reduct_native_fn native)
{
    assert(reduct != NULL);

    reduct_atom_t* atom = reduct_atom_new(reduct, 0);
    atom->native = native;
    atom->flags |= REDUCT_ATOM_FLAG_NATIVE | REDUCT_ATOM_FLAG_NATIVE_CHECKED;
    return atom;
}

static inline void reduct_atom_normalize_escape(reduct_atom_t* atom)
{
    assert(atom != NULL);

    char* str = atom->string;
    size_t len = atom->length;
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (str[i] == '\\' && i + 1 < len)
        {
            i++;
            const reduct_char_info_t* info = &reductCharTable[(unsigned char)str[i]];
            if (info->decodeEscape != 0)
            {
                str[j++] = info->decodeEscape;
                continue;
            }
            else if (str[i] == 'x' && i + 2 < len)
            {
                unsigned char high = reductCharTable[(unsigned char)str[i + 1]].integer;
                unsigned char low = reductCharTable[(unsigned char)str[i + 2]].integer;
                str[j++] = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        else
        {
            str[j++] = str[i];
        }
    }
    atom->length = j;
}

REDUCT_API bool reduct_atom_intern(reduct_t* reduct, reduct_atom_t* atom)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (atom->index != REDUCT_ATOM_INDEX_NONE)
    {
        return true;
    }

    uint32_t hash = reduct_hash(atom->string, atom->length);
    reduct_atom_lookup_flags_t flags = reduct_atom_get_lookup_flags(atom);

    reduct_rwmutex_read_lock(&reduct->global->atom.mutex);
    if (REDUCT_LIKELY(reduct->global->atom.capacity != 0))
    {
        size_t index = reduct_atom_map_find(reduct, hash, atom->string, atom->length, flags);
        if (reduct_atom_map_is_alive(reduct->global->atom.map[index]))
        {
            reduct_rwmutex_read_unlock(&reduct->global->atom.mutex);
            return false;
        }
    }
    reduct_rwmutex_read_unlock(&reduct->global->atom.mutex);

    reduct_rwmutex_write_lock(&reduct->global->atom.mutex);
    size_t index = reduct_atom_map_find_or_alloc(reduct, hash, atom->string, atom->length, flags);
    if (reduct_atom_map_is_alive(reduct->global->atom.map[index]))
    {
        atom->index = (uint32_t)index;
        reduct_rwmutex_write_unlock(&reduct->global->atom.mutex);
        return false;
    }

    reduct->global->atom.map[index] = atom;
    atom->index = (uint32_t)index;
    atom->hash = hash;
    reduct_rwmutex_write_unlock(&reduct->global->atom.mutex);
    return true;
}

REDUCT_API reduct_atom_t* reduct_atom_lookup(reduct_t* reduct, const char* str, size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);

    uint32_t hash = reduct_hash(str, len);

    reduct_rwmutex_read_lock(&reduct->global->atom.mutex);
    if (REDUCT_LIKELY(reduct->global->atom.capacity != 0))
    {
        size_t index = reduct_atom_map_find(reduct, hash, str, len, flags);
        reduct_atom_t* atom = reduct->global->atom.map[index];
        if (reduct_atom_map_is_alive(atom))
        {
            reduct_rwmutex_read_unlock(&reduct->global->atom.mutex);
            return atom;
        }
    }
    reduct_rwmutex_read_unlock(&reduct->global->atom.mutex);

    reduct_rwmutex_write_lock(&reduct->global->atom.mutex);

    size_t index = reduct_atom_map_find_or_alloc(reduct, hash, str, len, flags);
    if (reduct_atom_map_is_alive(reduct->global->atom.map[index]))
    {
        reduct_atom_t* atom = reduct->global->atom.map[index];
        reduct_rwmutex_write_unlock(&reduct->global->atom.mutex);
        return atom;
    }

    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, str, len);

    atom->hash = hash;
    atom->index = (uint32_t)index;
    reduct->global->atom.map[index] = atom;

    if (flags & REDUCT_ATOM_LOOKUP_QUOTED)
    {
        atom->flags |= REDUCT_ATOM_FLAG_QUOTED;

        reduct_atom_normalize_escape(atom);
        uint32_t hash = reduct_hash(atom->string, atom->length);
        reduct_atom_map_update(reduct, atom, index, hash);
    }

    reduct_rwmutex_write_unlock(&reduct->global->atom.mutex);
    return atom;
}

REDUCT_API reduct_atom_lookup_flags_t reduct_atom_get_lookup_flags(reduct_atom_t* atom)
{
    assert(atom != NULL);

    reduct_atom_lookup_flags_t flags = REDUCT_ATOM_LOOKUP_NONE;
    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
    {
        flags |= REDUCT_ATOM_LOOKUP_QUOTED;
    }
    return flags;
}

#define REDUCT_ATOM_MAX_NUMBER_LENGTH 70

REDUCT_API void reduct_atom_check_number(reduct_atom_t* atom)
{
    assert(atom != NULL);

    if (atom->flags & REDUCT_ATOM_FLAG_NUMBER_CHECKED)
    {
        return;
    }
    atom->flags |= REDUCT_ATOM_FLAG_NUMBER_CHECKED;

    if (atom->length == 0 || atom->length >= REDUCT_ATOM_MAX_NUMBER_LENGTH)
    {
        return;
    }

    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
    {
        return;
    }

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    const char* p = atom->string;
    const char* end = p + atom->length;
    const char* start = p;
    int sign = 1;

    if (p < end && (*p == '+' || *p == '-'))
    {
        if (*p == '-')
        {
            sign = -1;
        }
        p++;
    }

    if (p == end)
    {
        return;
    }

    if (end - p == 3)
    {
        if (REDUCT_CHAR_TO_LOWER(p[0]) == 'i' && REDUCT_CHAR_TO_LOWER(p[1]) == 'n' && REDUCT_CHAR_TO_LOWER(p[2]) == 'f')
        {
            atom->flags |= REDUCT_ATOM_FLAG_NUMBER;
            atom->numberValue = sign > 0 ? REDUCT_INF : -REDUCT_INF;
            return;
        }
        if (p == start && REDUCT_CHAR_TO_LOWER(p[0]) == 'n' && REDUCT_CHAR_TO_LOWER(p[1]) == 'a' &&
            REDUCT_CHAR_TO_LOWER(p[2]) == 'n')
        {
            atom->flags |= REDUCT_ATOM_FLAG_NUMBER;
            atom->numberValue = REDUCT_NAN;
            return;
        }
    }

    int base = 10;
    if (p + 1 < end && *p == '0')
    {
        char l = REDUCT_CHAR_TO_LOWER(p[1]);
        if (l == 'x')
        {
            base = 16;
            p += 2;
        }
        else if (l == 'o')
        {
            base = 8;
            p += 2;
        }
        else if (l == 'b')
        {
            base = 2;
            p += 2;
        }
    }

    bool hasDigits = false;
    bool sectionHasDigits = false;
    bool valid = true;

    if (base != 10)
    {
        uint64_t intValue = 0;
        while (p < end)
        {
            if (*p == '_')
            {
                if (!sectionHasDigits)
                {
                    valid = false;
                    break;
                }
                p++;
                continue;
            }

            int d = -1;
            unsigned char c = (unsigned char)*p;
            if (REDUCT_CHAR_IS_HEX_DIGIT(c))
            {
                d = reductCharTable[c].integer;
            }

            if (d >= 0 && d < base)
            {
                intValue = intValue * base + d;
                hasDigits = true;
                sectionHasDigits = true;
            }
            else
            {
                valid = false;
                break;
            }
            p++;
        }

        if (valid && hasDigits && p == end && *(end - 1) != '_')
        {
            atom->flags |= REDUCT_ATOM_FLAG_NUMBER;
            atom->numberValue = sign * (double)intValue;
        }

        return;
    }

    bool isFloat = false;
    uint64_t intValue = 0;
    double floatValue = 0.0;
    double fractionDiv = 10.0;
    bool inFraction = false;
    bool inExponent = false;
    int64_t expSign = 1;
    int64_t expValue = 0;
    int64_t exponentDigits = 0;

    if (*p == '.')
    {
        isFloat = true;
        inFraction = true;
        p++;
    }

    while (p < end)
    {
        if (*p == '_')
        {
            if (!sectionHasDigits)
            {
                valid = false;
                break;
            }
            p++;
            continue;
        }

        unsigned char c = (unsigned char)*p;
        if (REDUCT_CHAR_IS_DIGIT(c))
        {
            hasDigits = true;
            sectionHasDigits = true;
            if (inExponent)
            {
                expValue = expValue * 10 + reductCharTable[c].integer;
                exponentDigits++;
            }
            else if (inFraction)
            {
                floatValue = floatValue + reductCharTable[c].integer / fractionDiv;
                fractionDiv *= 10.0;
            }
            else
            {
                intValue = intValue * 10 + reductCharTable[c].integer;
                floatValue = floatValue * 10.0 + reductCharTable[c].integer;
            }
        }
        else if (c == '.' && !inFraction && !inExponent)
        {
            if (*(p - 1) == '_')
            {
                valid = false;
                break;
            }
            isFloat = true;
            inFraction = true;
            sectionHasDigits = false;
        }
        else if (REDUCT_CHAR_TO_LOWER(c) == 'e' && !inExponent && hasDigits)
        {
            if (*(p - 1) == '_')
            {
                valid = false;
                break;
            }
            isFloat = true;
            inExponent = true;
            sectionHasDigits = false;
            p++;
            if (p < end && (*p == '+' || *p == '-'))
            {
                if (*p == '-')
                {
                    expSign = -1;
                }
                p++;
            }
            continue;
        }
        else
        {
            valid = false;
            break;
        }
        p++;
    }

    if (inExponent && exponentDigits == 0)
    {
        valid = false;
    }

    if (valid && hasDigits && p == end && *(end - 1) != '_')
    {
        if (isFloat)
        {
            atom->flags |= REDUCT_ATOM_FLAG_NUMBER;
            double finalVal = floatValue;
            if (inExponent && expValue != 0)
            {
                double eMult = 1.0;
                double baseMult = 10.0;
                int e = expValue;
                while (e > 0)
                {
                    if (e % 2 != 0)
                    {
                        eMult *= baseMult;
                    }
                    baseMult *= baseMult;
                    e /= 2;
                }
                if (expSign < 0)
                {
                    finalVal /= eMult;
                }
                else
                {
                    finalVal *= eMult;
                }
            }
            atom->numberValue = sign * finalVal;
            return;
        }

        atom->flags |= REDUCT_ATOM_FLAG_NUMBER;
        atom->numberValue = sign * (double)intValue;
    }
}

REDUCT_API void reduct_atom_retain(reduct_t* reduct, reduct_atom_t* atom)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    reduct_item_retain(REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
}

REDUCT_API void reduct_atom_release(reduct_t* reduct, reduct_atom_t* atom)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    reduct_item_release(REDUCT_CONTAINER_OF(atom, reduct_item_t, atom));
}

REDUCT_API void reduct_atom_check_native(reduct_t* reduct, reduct_atom_t* atom)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (atom->flags & REDUCT_ATOM_FLAG_NATIVE_CHECKED)
    {
        return;
    }
    atom->flags |= REDUCT_ATOM_FLAG_NATIVE_CHECKED;

    uint32_t hash = atom->hash;
    if (hash == 0)
    {
        hash = reduct_hash(atom->string, atom->length);
    }

    reduct_native_entry_t* entry = reduct_native_map_find(reduct, hash, atom->string, atom->length);
    if (entry == NULL)
    {
        return;
    }

    if (entry->nativeFn != NULL)
    {
        atom->native = entry->nativeFn;
        atom->flags |= REDUCT_ATOM_FLAG_NATIVE;
    }

    if (entry->intrinsicFn != NULL)
    {
        atom->intrinsic = entry->intrinsicFn;
        atom->flags |= REDUCT_ATOM_FLAG_INTRINSIC;
    }
}

REDUCT_API reduct_atom_t* reduct_atom_substr(struct reduct* reduct, reduct_atom_t* atom, size_t start, size_t len)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (len == 0)
    {
        return reduct_atom_new(reduct, 0);
    }

    if (start == 0 && len == atom->length)
    {
        return atom;
    }

    assert(start + len <= atom->length);

    const char* str = atom->string;

    reduct_item_t* subItem = reduct_item_new(reduct);
    subItem->type = REDUCT_ITEM_TYPE_ATOM;

    reduct_atom_t* subAtom = &subItem->atom;
    subAtom->length = len;
    subAtom->hash = 0;
    subAtom->index = REDUCT_ATOM_INDEX_NONE;
    if (atom->flags & REDUCT_ATOM_FLAG_LARGE)
    {
        subAtom->flags = REDUCT_ATOM_FLAG_LARGE;
        subAtom->string = (char*)str + start;
        subAtom->stack = atom->stack;
    }
    else
    {
        subAtom->flags = 0;
        subAtom->string = subAtom->smallString;
        memcpy(subAtom->string, str + start, len);
    }
    return subAtom;
}

REDUCT_API reduct_atom_t* reduct_atom_superstr(struct reduct* reduct, reduct_atom_t* atom, size_t len)
{
    assert(reduct != NULL);
    assert(atom != NULL);
    assert(len > atom->length);

    if (len == 0)
    {
        return reduct_atom_new(reduct, 0);
    }

    if (atom->flags & REDUCT_ATOM_FLAG_LARGE && atom->stack != NULL)
    {
        reduct_atom_stack_t* stack = atom->stack;
        if (stack->data + stack->count == atom->string + atom->length &&
            stack->count + len - atom->length <= stack->capacity)
        {
            stack->count += len - atom->length;

            reduct_item_t* superItem = reduct_item_new(reduct);
            superItem->type = REDUCT_ITEM_TYPE_ATOM;

            reduct_atom_t* superAtom = &superItem->atom;
            superAtom->length = len;
            superAtom->hash = 0;
            superAtom->index = REDUCT_ATOM_INDEX_NONE;
            superAtom->flags = REDUCT_ATOM_FLAG_LARGE;
            superAtom->string = atom->string;
            superAtom->stack = atom->stack;
            return superAtom;
        }
    }

    reduct_atom_t* superAtom = reduct_atom_new(reduct, len);
    memcpy(superAtom->string, atom->string, atom->length);
    return superAtom;
}

REDUCT_API int64_t reduct_atom_as_int(struct reduct* reduct, reduct_atom_t* atom)
{
    if (reduct_atom_is_number(atom))
    {
        return (int64_t)reduct_atom_get_number(atom);
    }

    reduct_atom_t* unquoted = reduct_atom_lookup(reduct, atom->string, atom->length, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(unquoted))
    {
        return (int64_t)reduct_atom_get_number(unquoted);
    }

    return 0;
}

REDUCT_API double reduct_atom_as_number(struct reduct* reduct, reduct_atom_t* atom)
{
    if (reduct_atom_is_number(atom))
    {
        return reduct_atom_get_number(atom);
    }

    reduct_atom_t* unquoted = reduct_atom_lookup(reduct, atom->string, atom->length, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_is_number(unquoted))
    {
        return reduct_atom_get_number(unquoted);
    }

    return 0.0;
}
