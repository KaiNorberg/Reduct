#define REDUCT_INLINE
#include "reduct/reduct.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) 
{
    reduct_t* reduct = NULL;

    reduct_error_t error = REDUCT_ERROR();

    if (REDUCT_ERROR_CATCH(&error)) 
    {
        reduct_free(reduct);
        return 0;
    }

    reduct = reduct_new(&error);

    reduct_handle_t parsed = reduct_parse(reduct, (const char*)data, size, "<test>");
    reduct_handle_t function = reduct_compile(reduct, parsed);

    reduct_free(reduct);
    return 0;
}
