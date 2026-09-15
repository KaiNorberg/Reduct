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
 * The evaluator is a register-based virtual machine executing the bytecode produced by the emitter.
 *
 * @warning Calls are zero-copy such that the callee's frame is placed directly over the caller's argument registers, so
 * arguments become the callee's starting registers, and tail calls reuse the current frame.
 *
 * @warning Registers and frames are held in arrays that are realloc'd as frames are pushed and popped. Any pointer into
 * them, most notably the argv pointer passed to native functions, is only valid for the duration of the call that
 * produced it. Native functions that call back into Reduct must copy the handles they need out of argv before doing so.
 *
 * @{
 */

#define REDUCT_EVAL_REGS_INITIAL 64      ///< The initial amount of registers.
#define REDUCT_EVAL_REGS_GROWTH_FACTOR 2 ///< The growth factor of the registers array.

#define REDUCT_EVAL_FRAMES_INITIAL 32      ///< The initial size of the frames array.
#define REDUCT_EVAL_FRAMES_GROWTH_FACTOR 2 ///< The growth factor of the frames array.

/**
 * @brief Evaluation frame structure.
 * @struct reduct_eval_frame_t
 */
typedef struct reduct_eval_frame
{
    struct reduct_closure* closure; ///< The closure being evaluated.
    reduct_inst_t* ip;              ///< The current instruction pointer.
    reduct_handle_t* constants;     ///< Cached pointer to closure constants.
    uint32_t base;                  ///< The base register, where the functions registers start.
    uint32_t prevRegCount;          ///< The previous register count to restore upon return.
} reduct_eval_frame_t;

/**
 * @brief Per-thread eval-related state structure.
 * @struct reduct_eval_local_t
 */
typedef struct reduct_eval_state
{
    struct reduct_eval_frame* frames;
    size_t frameCount;
    size_t frameCapacity;
    reduct_handle_t* regs;
    size_t regCount;
    size_t regCapacity;
} reduct_eval_local_t;

/**
 * @brief Initialize a local eval state.
 *
 * @param local Pointer to the local eval state to initialize.
 */
REDUCT_API void reduct_eval_local_init(reduct_eval_local_t* local);

/**
 * @brief Deinitialize a local eval state.
 *
 * @param local Pointer to the local eval state to deinitialize.
 */
REDUCT_API void reduct_eval_local_deinit(reduct_eval_local_t* local);

/**
 * @brief Evaluates a handle.
 *
 * If the handle is a compiled function then it will be interpreted, otherwise, it will first be compiled into a
 * function.
 *
 * @warning May be called re-entrantly, including from within a native function. Doing so may grow the register and
 * frame arrays, invalidating any pointers previously obtained into them.
 *
 * @param reduct The Reduct instance.
 * @param handle The handle to evaluate.
 * @return The result of the evaluation as a Reduct handle.
 */
REDUCT_API reduct_handle_t reduct_eval(struct reduct* reduct, reduct_handle_t handle);

/**
 * @brief Parses, builds, optimizes, emits and evaluates a file.
 *
 * @warning May be called re-entrantly, including from within a native function. Doing so may grow the register and
 * frame arrays, invalidating any pointers previously obtained into them.
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
 * @warning May be called re-entrantly, including from within a native function. Doing so may grow the register and
 * frame arrays, invalidating any pointers previously obtained into them.
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
 * @warning May be called re-entrantly, including from within a native function. Doing so may grow the register and
 * frame arrays, invalidating any pointers previously obtained into them.
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
 * @warning May be called re-entrantly, including from within a native function. Doing so may grow the register and
 * frame arrays, invalidating any pointers previously obtained into them.
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
