#ifndef REDUCT_STANDARD_H
#define REDUCT_STANDARD_H 1

#include <reduct/defs.h>
#include <reduct/item.h>

struct reduct;

/**
 * @file standard.h
 * @brief Built-in library registration and operations.
 * @defgroup stdlib Stdlib
 *
 * The standard library provide a set of pre-defined native functions for use in Reduct expressions.
 *
 * @see native
 *
 * @{
 */

/**
 * @brief Built-in library sets.
 */
typedef enum
{
    REDUCT_STDLIB_STATE = (1 << 0),
    REDUCT_STDLIB_ERROR = (1 << 1),
    REDUCT_STDLIB_HIGHER_ORDER = (1 << 2),
    REDUCT_STDLIB_SEQUENCES = (1 << 3),
    REDUCT_STDLIB_STRING = (1 << 4),
    REDUCT_STDLIB_INTROSPECTION = (1 << 5),
    REDUCT_STDLIB_TYPE_CASTING = (1 << 6),
    REDUCT_STDLIB_SYSTEM = (1 << 7),
    REDUCT_STDLIB_MATH = (1 << 8),
    REDUCT_STDLIB_ALL = 0xFFFF,
} reduct_stdlib_sets_t;

REDUCT_API reduct_handle_t reduct_assert(struct reduct* reduct, reduct_handle_t state, reduct_handle_t cond,
    reduct_handle_t msg);
REDUCT_API reduct_handle_t reduct_throw(struct reduct* reduct, reduct_handle_t msg);
REDUCT_API reduct_handle_t reduct_try(struct reduct* reduct, reduct_handle_t callable, reduct_handle_t catchFn);

REDUCT_API reduct_handle_t reduct_map(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_filter(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_reduce(struct reduct* reduct, reduct_handle_t list, reduct_handle_t initial,
    reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_apply(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_any(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_all(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_sort(struct reduct* reduct, reduct_handle_t list, reduct_handle_t callable);

REDUCT_API reduct_handle_t reduct_len(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_range(struct reduct* reduct, reduct_handle_t start, reduct_handle_t end,
    reduct_handle_t step);
REDUCT_API reduct_handle_t reduct_concat(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_append(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_prepend(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_first(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_last(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_rest(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_init(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_nth(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t defaultVal);
REDUCT_API reduct_handle_t reduct_assoc(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t value, reduct_handle_t fillVal);
REDUCT_API reduct_handle_t reduct_dissoc(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index);
REDUCT_API reduct_handle_t reduct_update(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t index,
    reduct_handle_t callable, reduct_handle_t fillVal);
REDUCT_API reduct_handle_t reduct_index_of(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t target);
REDUCT_API reduct_handle_t reduct_reverse(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_slice(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t start,
    reduct_handle_t end);
REDUCT_API reduct_handle_t reduct_flatten(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t depth);
REDUCT_API reduct_handle_t reduct_contains(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t target);
REDUCT_API reduct_handle_t reduct_replace(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t oldVal,
    reduct_handle_t newVal);
REDUCT_API reduct_handle_t reduct_unique(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_chunk(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t size);
REDUCT_API reduct_handle_t reduct_find(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_get_in(struct reduct* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t defaultVal);
REDUCT_API reduct_handle_t reduct_assoc_in(struct reduct* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_dissoc_in(struct reduct* reduct, reduct_handle_t list, reduct_handle_t path);
REDUCT_API reduct_handle_t reduct_update_in(struct reduct* reduct, reduct_handle_t list, reduct_handle_t path,
    reduct_handle_t callable);
REDUCT_API reduct_handle_t reduct_keys(struct reduct* reduct, reduct_handle_t list);
REDUCT_API reduct_handle_t reduct_values(struct reduct* reduct, reduct_handle_t list);
REDUCT_API reduct_handle_t reduct_merge(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_explode(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_implode(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_repeat(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t count);

REDUCT_API reduct_handle_t reduct_starts_with(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t prefix);
REDUCT_API reduct_handle_t reduct_ends_with(struct reduct* reduct, reduct_handle_t handle, reduct_handle_t suffix);
REDUCT_API reduct_handle_t reduct_join(struct reduct* reduct, reduct_handle_t listHandle, reduct_handle_t sepHandle);
REDUCT_API reduct_handle_t reduct_split(struct reduct* reduct, reduct_handle_t srcHandle, reduct_handle_t sepHandle);
REDUCT_API reduct_handle_t reduct_upper(struct reduct* reduct, reduct_handle_t srcHandle);
REDUCT_API reduct_handle_t reduct_lower(struct reduct* reduct, reduct_handle_t srcHandle);
REDUCT_API reduct_handle_t reduct_trim(struct reduct* reduct, reduct_handle_t srcHandle);

REDUCT_API reduct_handle_t reduct_is_atom(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_number(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_lambda(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_native(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_callable(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_list(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_empty(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_is_nil(struct reduct* reduct, size_t argc, reduct_handle_t* argv);

REDUCT_API reduct_handle_t reduct_run(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_read_file(struct reduct* reduct, reduct_handle_t path);
REDUCT_API reduct_handle_t reduct_write_file(struct reduct* reduct, reduct_handle_t state, reduct_handle_t path,
    reduct_handle_t content);
REDUCT_API reduct_handle_t reduct_read_char(struct reduct* reduct);
REDUCT_API reduct_handle_t reduct_read_line(struct reduct* reduct);
REDUCT_API reduct_handle_t reduct_print(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_println(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_ord(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_chr(struct reduct* reduct, reduct_handle_t handle);
REDUCT_API reduct_handle_t reduct_format(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_now(struct reduct* reduct);
REDUCT_API reduct_handle_t reduct_uptime(struct reduct* reduct);
REDUCT_API reduct_handle_t reduct_env(struct reduct* reduct);
REDUCT_API reduct_handle_t reduct_args(struct reduct* reduct);

REDUCT_API reduct_handle_t reduct_min(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_max(struct reduct* reduct, size_t argc, reduct_handle_t* argv);
REDUCT_API reduct_handle_t reduct_clamp(struct reduct* reduct, reduct_handle_t val, reduct_handle_t minVal,
    reduct_handle_t maxVal);
REDUCT_API reduct_handle_t reduct_abs(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_floor(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_ceil(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_round(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_pow(struct reduct* reduct, reduct_handle_t base, reduct_handle_t exp);
REDUCT_API reduct_handle_t reduct_exp(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_log(struct reduct* reduct, reduct_handle_t val, reduct_handle_t base);
REDUCT_API reduct_handle_t reduct_sqrt(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_sin(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_cos(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_tan(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_asin(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_acos(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_atan(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_atan2(struct reduct* reduct, reduct_handle_t y, reduct_handle_t x);
REDUCT_API reduct_handle_t reduct_sinh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_cosh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_tanh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_asinh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_acosh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_atanh(struct reduct* reduct, reduct_handle_t val);
REDUCT_API reduct_handle_t reduct_rand(struct reduct* reduct, reduct_handle_t minVal, reduct_handle_t maxVal);
REDUCT_API reduct_handle_t reduct_seed(struct reduct* reduct, reduct_handle_t val);

/**
 * @brief Register a set from the standard library to the Reduct instance.
 *
 * @param reduct Pointer to the Reduct structure.
 * @param sets The sets to register.
 */
REDUCT_API void reduct_stdlib_register(struct reduct* reduct, reduct_stdlib_sets_t sets);

/** @} */

#endif
