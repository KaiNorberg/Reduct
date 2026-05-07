#include "reduct/closure.h"
#include "reduct/handle.h"
#include "reduct/item.h"

REDUCT_API reduct_closure_t* reduct_closure_new(struct reduct* reduct, reduct_function_t* function)
{
    assert(reduct != NULL);
    assert(function != NULL);

    reduct_item_t* item = reduct_item_new(reduct);
    item->type = REDUCT_ITEM_TYPE_CLOSURE;
    reduct_closure_t* closure = &item->closure;
    closure->function = function;
    if (function->constantCount <= REDUCT_CLOSURE_SMALL_MAX)
    {
        closure->constants = closure->smallConstants;
    }
    else
    {
        closure->constants = (reduct_handle_t*)malloc(sizeof(reduct_handle_t) * function->constantCount);
    }

    for (uint16_t i = 0; i < function->constantCount; i++)
    {
        if (function->constants[i].type != REDUCT_CONST_SLOT_ITEM)
        {
            closure->constants[i] = REDUCT_HANDLE_NONE;
            continue;
        }

        reduct_item_t* item = function->constants[i].item;
        if (item->type != REDUCT_ITEM_TYPE_ATOM)
        {
            closure->constants[i] = REDUCT_HANDLE_FROM_ITEM(item);
            continue;
        }

        reduct_atom_t* atom = &item->atom;
        if (reduct_atom_is_int(atom))
        {
            closure->constants[i] = REDUCT_HANDLE_FROM_INT(atom->integerValue);
        }
        else if (reduct_atom_is_float(atom))
        {
            closure->constants[i] = REDUCT_HANDLE_FROM_FLOAT(atom->floatValue);
        }
        else
        {
            closure->constants[i] = REDUCT_HANDLE_FROM_ITEM(item);
        }
    }

    return closure;
}