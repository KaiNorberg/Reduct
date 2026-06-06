#include <reduct/scratch.h>

REDUCT_API void reduct_scratch_local_init(reduct_scratch_local_t* local)
{
    assert(local != NULL);
    local->size = 0;
}

REDUCT_API void reduct_scratch_local_deinit(reduct_scratch_local_t* local)
{
    assert(local != NULL);
    for (size_t i = 0; i < local->size; i++)
    {
        free(local->buffers[i].buffer);
        local->buffers[i].buffer = NULL;
        local->buffers[i].length = 0;
    }
    local->size = 0;
}