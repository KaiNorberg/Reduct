#include "reduct/function.h"
#include "reduct/core.h"
#include "reduct/gc.h"
#include "reduct/handle.h"
#include "reduct/item.h"

REDUCT_API void reduct_function_init(reduct_function_t* func)
{
    assert(func != NULL);

    func->insts = NULL;
    func->positions = NULL;
    func->constants = NULL;
    func->instCount = 0;
    func->instCapacity = 0;
    func->constantCount = 0;
    func->constantCapacity = 0;
    func->registerCount = 0;
    func->arity = 0;
    func->flags = REDUCT_FUNCTION_FLAG_NONE;
    func->optimizeFlags = REDUCT_OPTIMIZE_NONE;
}

REDUCT_API void reduct_function_deinit(reduct_function_t* func)
{
    assert(func != NULL);

    if (func->insts != NULL)
    {
        free(func->insts);
    }
    if (func->positions != NULL)
    {
        free(func->positions);
    }
    if (func->constants != NULL)
    {
        free(func->constants);
    }
}

REDUCT_API reduct_function_t* reduct_function_new(reduct_t* reduct)
{
    assert(reduct != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_FUNCTION;
    reduct_function_t* func = &item->function;
    reduct_function_init(func);
    return func;
}

REDUCT_API void reduct_function_grow(reduct_t* reduct, reduct_function_t* func)
{
    assert(reduct != NULL);
    assert(func != NULL);

    size_t newCapacity = func->instCapacity == 0 ? 16 : func->instCapacity * 2;
    reduct_inst_t* newInsts = (reduct_inst_t*)realloc(func->insts, newCapacity * sizeof(reduct_inst_t));
    uint32_t* newPositions = (uint32_t*)realloc(func->positions, newCapacity * sizeof(uint32_t));

    if (newInsts == NULL || newPositions == NULL)
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
    assert(reduct != NULL);
    assert(func != NULL);
    assert(slot != NULL);

    for (reduct_const_t i = 0; i < func->constantCount; i++)
    {
        if (func->constants[i].type == slot->type && func->constants[i].raw == slot->raw)
        {
            return i;
        }
    }

    if (func->constantCount >= func->constantCapacity)
    {
        uint32_t newCapacity = func->constantCapacity == 0 ? 16 : func->constantCapacity * 2;
        if (newCapacity > REDUCT_CONSTANT_MAX)
        {
            REDUCT_ERROR_THROW(reduct, "too many constants in function");
        }
        reduct_const_slot_t* newConstants = realloc(func->constants, newCapacity * sizeof(reduct_const_slot_t));
        if (newConstants == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        func->constants = newConstants;
        func->constantCapacity = newCapacity;
    }

    func->constants[func->constantCount] = *slot;
    return func->constantCount++;
}

REDUCT_API void reduct_function_retain(reduct_t* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    assert(function != NULL);

    reduct_gc_retain(reduct, REDUCT_CONTAINER_OF(function, reduct_item_t, function));
}

REDUCT_API void reduct_function_release(reduct_t* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    assert(function != NULL);

    reduct_gc_release(reduct, REDUCT_CONTAINER_OF(function, reduct_item_t, function));
}
