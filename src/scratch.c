#include <reduct/scratch.h>

REDUCT_API void reduct_scratch_state_init(reduct_scratch_state_t* state)
{
    assert(state != NULL);
    state->size = 0;
}

REDUCT_API void reduct_scratch_state_deinit(reduct_scratch_state_t* state)
{
    assert(state != NULL);
    for (size_t i = 0; i < state->size; i++)
    {
        free(state->buffers[i].buffer);
        state->buffers[i].buffer = NULL;
        state->buffers[i].length = 0;
    }
    state->size = 0;
}