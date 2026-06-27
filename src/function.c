#include <reduct/core.h>
#include <reduct/function.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>

REDUCT_API void reduct_function_init(reduct_function_t* func)
{
    assert(func != NULL);

    func->insts = NULL;
    func->sources = NULL;
    func->constants = NULL;
    func->instCount = 0;
    func->instCapacity = 0;
    func->constantCount = 0;
    func->constantCapacity = 0;
    func->captureCount = 0;
    func->registerCount = 0;
    func->arity = 0;
    func->flags = REDUCT_FUNCTION_FLAG_NONE;
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
    reduct_function_inst_source_t* newSources =
        (reduct_function_inst_source_t*)realloc(func->sources, newCapacity * sizeof(reduct_function_inst_source_t));
    if (newInsts == NULL || newSources == NULL)
    {
        REDUCT_ERROR_INTERNAL(reduct, "out of memory");
    }

    func->insts = newInsts;
    func->sources = newSources;
    func->instCapacity = newCapacity;
}

REDUCT_API void reduct_function_set_capture_count(reduct_t* reduct, reduct_function_t* func, uint16_t captureCount)
{
    assert(reduct != NULL);
    assert(func != NULL);
    assert(func->captureCount == 0 && func->constantCount == 0 &&
        "capture count must be reserved exactly once, before any constants are added");

    if (captureCount > REDUCT_CONSTANT_MAX)
    {
        REDUCT_ERROR_THROW(reduct, "too many captures in function");
    }

    func->captureCount = captureCount;
}

static inline uint16_t reduct_function_grow_constants(reduct_t* reduct, reduct_function_t* func)
{
    if (func->constantCount >= func->constantCapacity)
    {
        uint32_t newCapacity = func->constantCapacity == 0 ? 16 : func->constantCapacity * 2;
        if ((uint32_t)func->captureCount + newCapacity > REDUCT_CONSTANT_MAX)
        {
            REDUCT_ERROR_THROW(reduct, "too many constants in function");
        }
        reduct_handle_t* newConstants = realloc(func->constants, newCapacity * sizeof(reduct_handle_t));
        if (newConstants == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
        func->constants = newConstants;
        func->constantCapacity = (uint16_t)newCapacity;
    }
    return func->constantCount++;
}

REDUCT_API reduct_const_t reduct_function_add_constant(reduct_t* reduct, reduct_function_t* func,
    reduct_handle_t handle)
{
    assert(reduct != NULL);
    assert(func != NULL);

    for (uint16_t i = 0; i < func->constantCount; i++)
    {
        if (reduct_handle_is_equal(reduct, func->constants[i], handle))
        {
            return (reduct_const_t)(func->captureCount + i);
        }
    }

    uint16_t index = reduct_function_grow_constants(reduct, func);
    func->constants[index] = handle;
    return (reduct_const_t)(func->captureCount + index);
}

REDUCT_API void reduct_function_retain(reduct_t* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    if (function == NULL)
    {
        return;
    }

    reduct_item_retain(REDUCT_CONTAINER_OF(function, reduct_item_t, function));
}

REDUCT_API void reduct_function_release(reduct_t* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    if (function == NULL)
    {
        return;
    }

    reduct_item_release(REDUCT_CONTAINER_OF(function, reduct_item_t, function));
}