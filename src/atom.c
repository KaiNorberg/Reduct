#include "reduct/item.h"
#include "reduct/defs.h"
#include "reduct/atom.h"
#include "reduct/char.h"
#include "reduct/core.h"

#include <stdint.h>
#include <string.h>

#define REDUCT_ATOM_TOMBSTONE ((reduct_atom_t*)(uintptr_t)1)

static inline reduct_bool_t reduct_atom_map_is_alive(reduct_atom_t* slot)
{
    return slot != NULL && slot != REDUCT_ATOM_TOMBSTONE;
}

static inline size_t reduct_atom_map_find(
    reduct_t* reduct,
    uint32_t hash,
    const char* str,
    size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);
    assert(reduct->atomMapCapacity != 0);
    assert((reduct->atomMapCapacity & (reduct->atomMapCapacity - 1)) == 0);
    assert(reduct->atomMapSize <= reduct->atomMapCapacity);

    reduct_bool_t wantQuoted = (flags & REDUCT_ATOM_LOOKUP_QUOTED) != 0;

    size_t index = hash & reduct->atomMapMask;
    size_t step = 1;

    size_t tombstoneIndex = REDUCT_ATOM_INDEX_NONE;

    while (reduct->atomMap[index] != NULL)
    {
        reduct_atom_t* atom = reduct->atomMap[index];

        if (atom == REDUCT_ATOM_TOMBSTONE)
        {
            if (tombstoneIndex == REDUCT_ATOM_INDEX_NONE)
            {
                tombstoneIndex = index;
            }
        }
        else
        {
            reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
            reduct_bool_t isQuoted = (item->flags & REDUCT_ITEM_FLAG_QUOTED) != 0;

            if (atom->hash == hash && isQuoted == wantQuoted && reduct_atom_is_equal(atom, str, len))
            {
                return index;
            }
        }

        index = (index + step) & reduct->atomMapMask;
        step++;
    }

    if (tombstoneIndex != REDUCT_ATOM_INDEX_NONE)
    {
        return tombstoneIndex;
    }

    return index;
}

static inline size_t reduct_atom_map_find_or_alloc(
    reduct_t* reduct,
    uint32_t hash,
    const char* str,
    size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);

    if (reduct->atomMapCapacity == 0)
    {
        reduct->atomMapCapacity = REDUCT_ATOM_MAP_INITIAL;
        reduct->atomMapMask = reduct->atomMapCapacity - 1;
        reduct->atomMapTombstones = 0;
        reduct->atomMap = calloc(reduct->atomMapCapacity, sizeof(reduct_atom_t*));
        if (reduct->atomMap == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    size_t occupied = reduct->atomMapSize + reduct->atomMapTombstones;
    if (occupied * 4 > reduct->atomMapCapacity * 3)
    {
        size_t oldCapacity = reduct->atomMapCapacity;
        reduct_atom_t** oldMap = reduct->atomMap;

        reduct->atomMapCapacity = oldCapacity * REDUCT_ATOM_MAP_GROWTH;
        reduct->atomMapMask = reduct->atomMapCapacity - 1;
        reduct->atomMapTombstones = 0;
        reduct->atomMap = calloc(reduct->atomMapCapacity, sizeof(reduct_atom_t*));
        if (reduct->atomMap == NULL)
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

            size_t index = atom->hash & reduct->atomMapMask;
            size_t step = 1;
            while (reduct->atomMap[index] != NULL)
            {
                index = (index + step) & reduct->atomMapMask;
                step++;
            }

            reduct->atomMap[index] = atom;
            atom->index = (uint32_t)index;
        }

        if (oldMap != NULL)
        {
            free(oldMap);
        }
    }

    size_t index = reduct_atom_map_find(reduct, hash, str, len, flags);

    if (reduct_atom_map_is_alive(reduct->atomMap[index]))
    {
        return index;
    }

    if (reduct->atomMap[index] == REDUCT_ATOM_TOMBSTONE)
    {
        reduct->atomMapTombstones--;
    }

    reduct->atomMapSize++;
    return index;
}

static inline void reduct_atom_map_update(reduct_t* reduct, reduct_atom_t* atom, size_t newIndex, uint32_t hash)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (atom->index != REDUCT_ATOM_INDEX_NONE)
    {
        reduct->atomMap[atom->index] = REDUCT_ATOM_TOMBSTONE;
        reduct->atomMapTombstones++;
    }

    reduct->atomMap[newIndex] = atom;
    atom->index = (uint32_t)newIndex;
    atom->hash = hash;

}

REDUCT_API reduct_bool_t reduct_atom_is_equal(reduct_atom_t* atom, const char* str,
    size_t len)
{
    if (atom->length != len)
    {
        return REDUCT_FALSE;
    }
    if (atom->hash != reduct_hash(str, len))
    {
        return REDUCT_FALSE;
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

    stack->next = reduct->atomStack;
    stack->prev = NULL;
    if (reduct->atomStack != NULL)
    {
        reduct->atomStack->prev = stack;
    }
    reduct->atomStack = stack;
    return stack;
}

static inline char* reduct_atom_stack_alloc(reduct_t* reduct, size_t size, reduct_atom_stack_t** out)
{
    assert(reduct != NULL);

    reduct_atom_stack_t* stack = reduct->atomStack;
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

    if (len == 0)
    {
        item->flags |= REDUCT_ITEM_FLAG_FALSY;
    }

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

    size_t len = strlen(str);
    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, str, len);
    return atom;
}

REDUCT_API reduct_atom_t* reduct_atom_new_int(reduct_t* reduct, int64_t value)
{
    assert(reduct != NULL);

    char buf[32];
    size_t len = 0;

    unsigned long long uval;
    reduct_bool_t isNegative;
    if (value < 0)
    {
        isNegative = REDUCT_TRUE;
        uval = (unsigned long long)(-value);
    }
    else
    {
        isNegative = REDUCT_FALSE;
        uval = (unsigned long long)value;
    }

    if (uval == 0)
    {
        buf[sizeof(buf) - 1 - len++] = '0';
    }
    while (uval > 0)
    {
        buf[sizeof(buf) - 1 - len++] = (char)('0' + (uval % 10));
        uval /= 10;
    }
    if (isNegative)
    {
        buf[sizeof(buf) - 1 - len++] = '-';
    }

    const char* str = buf + sizeof(buf) - len;
    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, str, len);
    atom->integerValue = value;
    atom->flags |= REDUCT_ATOM_FLAG_INTEGER | REDUCT_ATOM_FLAG_NUMBER_CHECKED;
    if (value == 0)
    {
        reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
        item->flags |= REDUCT_ITEM_FLAG_FALSY;
    }
    return atom;
}

REDUCT_API reduct_atom_t* reduct_atom_new_float(reduct_t* reduct, double value)
{
    assert(reduct != NULL);

    char buf[64];
    char sign = 1;
    double val = value;

    if (val < 0)
    {
        sign = -1;
        val = -val;
    }

    val += 0.0000005;
    unsigned long long intPart = (unsigned long long)val;
    double fracPart = val - (double)intPart;
    if (fracPart < 0)
    {
        fracPart = 0;
    }

    size_t len = 0;
    char* p = buf + 32;
    unsigned long long uIntPart = intPart;

    if (uIntPart == 0)
    {
        *--p = '0';
        len++;
    }
    else
    {
        while (uIntPart > 0)
        {
            *--p = (char)('0' + (uIntPart % 10));
            uIntPart /= 10;
            len++;
        }
    }
    if (sign == -1)
    {
        *--p = '-';
        len++;
    }

    char* res = p;
    res[len++] = '.';

    for (int i = 0; i < 6; i++)
    {
        fracPart *= 10.0;
        int digit = (int)fracPart;
        res[len++] = (char)('0' + digit);
        fracPart -= (double)digit;
    }

    while (len > 1 && res[len - 1] == '0' && res[len - 2] != '.')
    {
        len--;
    }

    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, res, len);
    atom->floatValue = value;
    atom->flags |= REDUCT_ATOM_FLAG_FLOAT | REDUCT_ATOM_FLAG_NUMBER_CHECKED;
    if (value == 0.0)
    {
        reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
        item->flags |= REDUCT_ITEM_FLAG_FALSY;
    }
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

REDUCT_API void reduct_atom_intern(reduct_t* reduct, reduct_atom_t* atom)
{
    assert(reduct != NULL);
    assert(atom != NULL);

    if (atom->index != REDUCT_ATOM_INDEX_NONE)
    {
        return;
    }

    atom->hash = reduct_hash(atom->string, atom->length);
    size_t index = reduct_atom_map_find_or_alloc(reduct, atom->hash, atom->string, atom->length, REDUCT_ATOM_LOOKUP_NONE);
    if (reduct_atom_map_is_alive(reduct->atomMap[index]))
    {
        REDUCT_ERROR_INTERNAL(reduct, "atom already interned");
    }

    reduct->atomMap[atom->index] = REDUCT_ATOM_TOMBSTONE;
    reduct->atomMapTombstones++;

    reduct->atomMap[index] = atom;
    atom->index = (uint32_t)index;
}

REDUCT_API reduct_atom_t* reduct_atom_lookup(reduct_t* reduct, const char* str, size_t len,
    reduct_atom_lookup_flags_t flags)
{
    assert(reduct != NULL);
    assert(str != NULL);

    uint32_t hash = reduct_hash(str, len);
    size_t index = reduct_atom_map_find_or_alloc(reduct, hash, str, len, flags);
    if (reduct->atomMap[index] != NULL && reduct->atomMap[index] != REDUCT_ATOM_TOMBSTONE)
    {
        return reduct->atomMap[index];
    }

    reduct_atom_t* atom = reduct_atom_new(reduct, len);
    memcpy(atom->string, str, len);

    atom->hash = hash;
    atom->index = (uint32_t)index;
    reduct->atomMap[index] = atom;

    if (flags & REDUCT_ATOM_LOOKUP_QUOTED)
    {
        reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
        item->flags |= REDUCT_ITEM_FLAG_QUOTED;

        reduct_atom_normalize_escape(atom);
        uint32_t hash = reduct_hash(atom->string, atom->length);
        reduct_atom_map_update(reduct, atom, index, hash);
    }

    return atom;
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

    reduct_item_t* item = REDUCT_CONTAINER_OF(atom, reduct_item_t, atom);
    if (item->flags & REDUCT_ITEM_FLAG_QUOTED)
    {
        return;
    }

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
            atom->flags |= REDUCT_ATOM_FLAG_FLOAT;
            atom->floatValue = sign > 0 ? REDUCT_INF : -REDUCT_INF;
            return;
        }
        if (p == start && REDUCT_CHAR_TO_LOWER(p[0]) == 'n' && REDUCT_CHAR_TO_LOWER(p[1]) == 'a' &&
            REDUCT_CHAR_TO_LOWER(p[2]) == 'n')
        {
            atom->flags |= REDUCT_ATOM_FLAG_FLOAT;
            atom->floatValue = REDUCT_NAN;
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

    reduct_bool_t hasDigits = REDUCT_FALSE;
    reduct_bool_t sectionHasDigits = REDUCT_FALSE;
    reduct_bool_t valid = REDUCT_TRUE;

    if (base != 10)
    {
        uint64_t intValue = 0;
        while (p < end)
        {
            if (*p == '_')
            {
                if (!sectionHasDigits)
                {
                    valid = REDUCT_FALSE;
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
                hasDigits = REDUCT_TRUE;
                sectionHasDigits = REDUCT_TRUE;
            }
            else
            {
                valid = REDUCT_FALSE;
                break;
            }
            p++;
        }

        if (valid && hasDigits && p == end && *(end - 1) != '_')
        {
            if (intValue > REDUCT_HANDLE_INT_MAX)
            {
                atom->flags |= REDUCT_ATOM_FLAG_OVERFLOW;
                return;
            }
            atom->flags |= REDUCT_ATOM_FLAG_INTEGER;
            atom->integerValue = sign * (int64_t)intValue;
            if (atom->integerValue == 0)
            {
                item->flags |= REDUCT_ITEM_FLAG_FALSY;
            }
        }

        return;
    }

    reduct_bool_t isFloat = REDUCT_FALSE;
    uint64_t intValue = 0;
    double floatValue = 0.0;
    double fractionDiv = 10.0;
    reduct_bool_t inFraction = REDUCT_FALSE;
    reduct_bool_t inExponent = REDUCT_FALSE;
    int64_t expSign = 1;
    int64_t expValue = 0;
    int64_t exponentDigits = 0;

    if (*p == '.')
    {
        isFloat = REDUCT_TRUE;
        inFraction = REDUCT_TRUE;
        p++;
    }

    while (p < end)
    {
        if (*p == '_')
        {
            if (!sectionHasDigits)
            {
                valid = REDUCT_FALSE;
                break;
            }
            p++;
            continue;
        }

        unsigned char c = (unsigned char)*p;
        if (REDUCT_CHAR_IS_DIGIT(c))
        {
            hasDigits = REDUCT_TRUE;
            sectionHasDigits = REDUCT_TRUE;
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
                valid = REDUCT_FALSE;
                break;
            }
            isFloat = REDUCT_TRUE;
            inFraction = REDUCT_TRUE;
            sectionHasDigits = REDUCT_FALSE;
        }
        else if (REDUCT_CHAR_TO_LOWER(c) == 'e' && !inExponent && hasDigits)
        {
            if (*(p - 1) == '_')
            {
                valid = REDUCT_FALSE;
                break;
            }
            isFloat = REDUCT_TRUE;
            inExponent = REDUCT_TRUE;
            sectionHasDigits = REDUCT_FALSE;
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
            valid = REDUCT_FALSE;
            break;
        }
        p++;
    }

    if (inExponent && exponentDigits == 0)
    {
        valid = REDUCT_FALSE;
    }

    if (valid && hasDigits && p == end && *(end - 1) != '_')
    {
        if (isFloat)
        {
            atom->flags |= REDUCT_ATOM_FLAG_FLOAT;
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
            atom->floatValue = sign * finalVal;
            if (atom->floatValue == 0.0)
            {
                item->flags |= REDUCT_ITEM_FLAG_FALSY;
            }

            return;
        }

        if (intValue > REDUCT_HANDLE_INT_MAX)
        {
            atom->flags |= REDUCT_ATOM_FLAG_OVERFLOW;
            return;
        }
        atom->flags |= REDUCT_ATOM_FLAG_INTEGER;
        atom->integerValue = sign * (int64_t)intValue;
        if (atom->integerValue == 0)
        {
            item->flags |= REDUCT_ITEM_FLAG_FALSY;
        }
    }
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

    atom->native = entry->nativeFn;
    atom->flags |= REDUCT_ATOM_FLAG_NATIVE;

    if (entry->intrinsicFn != NULL)
    {
        atom->intrinsic = entry->intrinsicFn;
        atom->flags |= REDUCT_ATOM_FLAG_INTRINSIC;
    }
}

REDUCT_API reduct_atom_t* reduct_atom_substr(struct reduct* reduct, reduct_atom_t* atom, size_t start,
    size_t len)
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
        if (stack->data + stack->count == atom->string + atom->length && stack->count + len - atom->length <= stack->capacity)
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