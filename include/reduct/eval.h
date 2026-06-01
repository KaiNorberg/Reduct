#ifndef REDUCT_EVAL_H
#define REDUCT_EVAL_H 1

#include <reduct/function.h>
#include <reduct/handle.h>
#include <reduct/optimize.h>

struct reduct_closure;

/**
 * @file eval.h
 * @brief Virtual machine evaluation.
 * @defgroup eval Evaluation
 *
 * @{
 */

#define REDUCT_EVAL_REGS_INITIAL 64      ///< The initial amount of registers.
#define REDUCT_EVAL_REGS_GROWTH_FACTOR 2 ///< The growth factor of the registers array.
#define REDUCT_EVAL_REGS_MAX 65536       ///< The maximum amount of registers.

#define REDUCT_EVAL_FRAMES_INITIAL 32      ///< The initial size of the frames array.
#define REDUCT_EVAL_FRAMES_GROWTH_FACTOR 2 ///< The growth factor of the frames array.
#define REDUCT_EVAL_FRAMES_MAX 65536       ///< The maximum size of the frames array.

/**
 * @brief Evaluation frame structure.
 * @struct reduct_eval_frame_t
 */
typedef struct reduct_eval_frame
{
    struct reduct_closure* closure; ///< The closure being evaluated.
    reduct_inst_t* ip;              ///< The current instruction pointer.
    uint32_t base;                  ///< The base register, where the functions registers start.
    uint32_t prevRegCount;          ///< The previous register count to restore upon return.
} reduct_eval_frame_t;

/**
 * @brief Per-thread eval-related state structure.
 * @struct reduct_eval_state_t
 */
typedef struct reduct_eval_state
{
    struct reduct_eval_frame* frames;
    size_t frameCount;
    size_t frameCapacity;
    reduct_handle_t* regs;
    size_t regCount;
    size_t regCapacity;
} reduct_eval_state_t;

/**
 * @brief Initialize an eval state.
 *
 * @param state Pointer to the eval state to initialize.
 */
REDUCT_API void reduct_eval_state_init(reduct_eval_state_t* state);

/**
 * @brief Deinitialize an eval state.
 *
 * @param state Pointer to the eval state to deinitialize.
 */
REDUCT_API void reduct_eval_state_deinit(reduct_eval_state_t* state);

/**
 * @brief Evaluates a handle.
 *
 * If the handle is a compiled function then it will be interpreted, otherwise, it will first be compiled into a
 * function.
 *
 * @param reduct The Reduct instance.
 * @param handle The handle to evaluate.
 * @return The result of the evaluation as a Reduct handle.
 */
REDUCT_API reduct_handle_t reduct_eval(struct reduct* reduct, reduct_handle_t handle);

/**
 * @brief Parses, builds, optimizes, emits and evaluates a file.
 *
 * @param reduct The Reduct instance.
 * @param path The path to the file.
 * @param flags Optimization flags to control which optimizations are applied.
 * @return The result of the evaluation.
 */
REDUCT_API reduct_handle_t reduct_eval_file(struct reduct* reduct, const char* path, reduct_optimize_flags_t optimize);

/**
 * @brief Parses, builds, optimizes, emits and evaluates a string.
 *
 * @param reduct The Reduct instance.
 * @param str The string to evaluate.
 * @param len The length of the string.
 * @param flags Optimization flags to control which optimizations are applied.
 * @return The result of the evaluation.
 */
REDUCT_API reduct_handle_t reduct_eval_string(struct reduct* reduct, const char* str, size_t len,
    reduct_optimize_flags_t optimize);

/**
 * @brief Calls a Reduct callable (closure or native) with arguments.
 *
 * @param reduct The Reduct instance.
 * @param callable The callable item handle.
 * @param argc The number of arguments.
 * @param argv Pointer to the arguments array.
 * @return The result of the call.
 */
REDUCT_API reduct_handle_t reduct_eval_call(struct reduct* reduct, reduct_handle_t callable, size_t argc,
    reduct_handle_t* argv);

/**
 * @brief Calls a Reduct callable (closure or native) with variadic arguments.
 *
 * @param reduct The Reduct instance.
 * @param callable The callable item handle.
 * @param argc The number of arguments.
 * @param ... The arguments (as reduct_handle_t).
 * @return The result of the call.
 */
REDUCT_API reduct_handle_t reduct_eval_call_v(struct reduct* reduct, reduct_handle_t callable, size_t argc, ...);

/** @} */

#endif
