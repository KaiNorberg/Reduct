#include <reduct/closure.h>
#include <reduct/core.h>
#include <reduct/gc.h>
#include <reduct/handle.h>
#include <reduct/item.h>

REDUCT_API reduct_closure_t* reduct_closure_new(struct reduct* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    assert(function != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_CLOSURE;

    reduct_closure_t* closure = &item->closure;
    closure->function = function;

    closure->constantCount = (uint32_t)function->captureCount + (uint32_t)function->constantCount;
    if (closure->constantCount <= REDUCT_CLOSURE_SMALL_MAX)
    {
        closure->constants = closure->smallConstants;
    }
    else
    {
        closure->constants = (reduct_handle_t*)malloc(sizeof(reduct_handle_t) * closure->constantCount);
        if (closure->constants == NULL)
        {
            REDUCT_ERROR_INTERNAL(reduct, "out of memory");
        }
    }

    for (uint16_t i = 0; i < function->captureCount; i++)
    {
        closure->constants[i] = REDUCT_HANDLE_NIL(reduct);
    }

    for (uint16_t i = 0; i < function->constantCount; i++)
    {
        reduct_handle_t handle = function->constants[i];

        if (REDUCT_HANDLE_IS_ATOM(handle))
        {
            reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
            if (reduct_atom_is_number(atom))
            {
                closure->constants[function->captureCount + i] =
                    REDUCT_HANDLE_FROM_NUMBER(reduct_atom_get_number(atom));
                continue;
            }
        }

        closure->constants[function->captureCount + i] = handle;
    }

    return closure;
}

REDUCT_API void reduct_closure_retain(reduct_t* reduct, reduct_closure_t* closure)
{
    assert(reduct != NULL);
    if (closure == NULL)
    {
        return;
    }

    reduct_item_retain(REDUCT_CONTAINER_OF(closure, reduct_item_t, closure));
}

REDUCT_API void reduct_closure_release(reduct_t* reduct, reduct_closure_t* closure)
{
    assert(reduct != NULL);
    if (closure == NULL)
    {
        return;
    }

    reduct_item_release(REDUCT_CONTAINER_OF(closure, reduct_item_t, closure));
}