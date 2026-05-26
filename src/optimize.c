#include <reduct/core.h>
#include <reduct/dump.h>
#include <reduct/function.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/list.h>
#include <reduct/optimize.h>

#include <stdio.h>
#include <string.h>

REDUCT_API void reduct_optimize(reduct_t* reduct, reduct_handle_t handle, reduct_optimize_flags_t flags)
{
    assert(reduct != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        return;
    }

    reduct_function_t* func = REDUCT_HANDLE_TO_FUNCTION(handle);
    if (func->instCount == 0)
    {
        return;
    }

    if (func->flags & REDUCT_FUNCTION_FLAG_OPTIMIZED)
    {
        return;
    }
    func->flags |= REDUCT_FUNCTION_FLAG_OPTIMIZED;
}