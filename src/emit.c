#include "reduct/function.h"
#include "reduct/handle.h"
#include "reduct/rvsdg.h"
#include <reduct/atom.h>
#include <reduct/emit.h>
#include <reduct/core.h>
#include <reduct/defs.h>
#include <reduct/gc.h>
#include <reduct/inst.h>
#include <reduct/intrinsic.h>
#include <reduct/item.h>
#include <reduct/list.h>

REDUCT_API reduct_handle_t reduct_emit(reduct_t* reduct, reduct_handle_t graph)
{
    if (!REDUCT_HANDLE_IS_RVSDG_NODE(graph))
    {
        REDUCT_ERROR_THROW(reduct, "expected RVSDG node, got %s", REDUCT_HANDLE_GET_TYPE_STRING(graph));
    }

    reduct_rvsdg_node_t* root = REDUCT_HANDLE_TO_RVSDG_NODE(graph);

    if (root->type != REDUCT_RVSDG_NODE_TYPE_LAMBDA)
    {
        const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(root->type);
        REDUCT_ERROR_THROW(reduct, "expected RVSDG lambda node as root, got %s", info->name);
    }

    reduct_function_t* function = reduct_function_new(reduct);

    reduct_emitter_t ctx = {
        .reduct = reduct,
        .function = function
    };



    return REDUCT_HANDLE_FROM_FUNCTION(function);
}