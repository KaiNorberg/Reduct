#define REDUCT_INLINE
#include <reduct/reduct.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) 
{
    reduct_t* reduct = reduct_new();
    if (reduct == NULL)
    {
        return 0;
    }

    reduct_error_t error = REDUCT_ERROR();
    REDUCT_ERROR_TRY(reduct, &error)
    {
        reduct_handle_t parsed = reduct_parse(reduct, (const char*)data, size, "<test>");
        reduct_handle_t graph = reduct_build(reduct, parsed);
        reduct_optimize(reduct, graph,  REDUCT_OPTIMIZE_ALL);
    }

    reduct_free(reduct);
    return 0;
}
