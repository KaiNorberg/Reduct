#include "reduct/core.h"
#include "reduct/function.h"
#include "reduct/gc.h"
#include "reduct/handle.h"
#include "reduct/item.h"

REDUCT_API void reduct_function_init(reduct_function_t* func)
{
    REDUCT_ASSERT(func != REDUCT_NULL);

    func->insts = REDUCT_NULL;
    func->positions = REDUCT_NULL;
    func->constants = REDUCT_NULL;
    func->instCount = 0;
    func->instCapacity = 0;
    func->constantCount = 0;
    func->constantCapacity = 0;
    func->registerCount = 0;
    func->arity = 0;
    func->flags = REDUCT_FUNCTION_FLAG_NONE;
}

REDUCT_API void reduct_function_deinit(reduct_function_t* func)
{
    REDUCT_ASSERT(func != REDUCT_NULL);

    if (func->insts != REDUCT_NULL)
    {
        REDUCT_FREE(func->insts);
    }
    if (func->positions != REDUCT_NULL)
    {
        REDUCT_FREE(func->positions);
    }
    if (func->constants != REDUCT_NULL)
    {
        REDUCT_FREE(func->constants);
    }
}

REDUCT_API reduct_function_t* reduct_function_new(reduct_t* reduct)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_FUNCTION;
    reduct_function_t* func = &item->function;
    reduct_function_init(func);
    return func;
}

REDUCT_API void reduct_function_grow(reduct_t* reduct, reduct_function_t* func)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(func != REDUCT_NULL);

    reduct_size_t newCapacity = func->instCapacity == 0 ? 16 : func->instCapacity * 2;
    reduct_inst_t* newInsts = (reduct_inst_t*)REDUCT_REALLOC(func->insts, newCapacity * sizeof(reduct_inst_t));
    reduct_uint32_t* newPositions =
        (reduct_uint32_t*)REDUCT_REALLOC(func->positions, newCapacity * sizeof(reduct_uint32_t));

    if (newInsts == REDUCT_NULL || newPositions == REDUCT_NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    func->insts = newInsts;
    func->positions = newPositions;
    func->instCapacity = newCapacity;
}

REDUCT_API reduct_const_t reduct_function_lookup_constant(reduct_t* reduct, reduct_function_t* func,
    reduct_const_slot_t* slot)
{
    REDUCT_ASSERT(reduct != REDUCT_NULL);
    REDUCT_ASSERT(func != REDUCT_NULL);
    REDUCT_ASSERT(slot != REDUCT_NULL);

    for (reduct_const_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == slot->type && func->constants[i].raw == slot->raw)
        {
            return i;
        }
    }

    if (func->constantCount >= func->constantCapacity)
    {
        reduct_uint32_t newCapacity = func->constantCapacity == 0 ? 16 : func->constantCapacity * 2;
        if (newCapacity > REDUCT_CONSTANT_MAX)
        {
            REDUCT_ERROR_RUNTIME(reduct, "too many constants in function");
        }
        reduct_const_slot_t* newConstants = REDUCT_REALLOC(func->constants, newCapacity * sizeof(reduct_const_slot_t));
        if (newConstants == REDUCT_NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        func->constants = newConstants;
        func->constantCapacity = newCapacity;
    }

    func->constants[func->constantCount] = *slot;
    return func->constantCount++;
}