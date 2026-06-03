#ifndef REDUCT_DEFS_H
#define REDUCT_DEFS_H 1

#include <stddef.h>
#include <stdint.h>

struct reduct;
struct reduct_list;
struct reduct_builder;
struct reduct_list;

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef REDUCT_BUILD_LIB
#define REDUCT_API __declspec(dllexport)
#elif defined(REDUCT_USE_LIB)
#define REDUCT_API __declspec(dllimport)
#else
#define REDUCT_API
#endif
#else
#if __GNUC__ >= 4
#define REDUCT_API __attribute__((visibility("default")))
#else
#define REDUCT_API
#endif
#endif

#ifdef _WIN32
#include <windows.h>
typedef HMODULE reduct_lib_t;
#define REDUCT_LIB_OPEN(_path) LoadLibraryA(_path)
#define REDUCT_LIB_CLOSE(_lib) FreeLibrary(_lib)
#define REDUCT_LIB_SYM(_lib, _name) GetProcAddress(_lib, _name)
#define REDUCT_LIB_ERROR() "Windows Error"
#else
#include <dlfcn.h>
#include <unistd.h>
typedef void* reduct_lib_t;
#define REDUCT_LIB_OPEN(_path) dlopen(_path, RTLD_NOW | RTLD_GLOBAL)
#define REDUCT_LIB_CLOSE(_lib) dlclose(_lib)
#define REDUCT_LIB_SYM(_lib, _name) dlsym(_lib, _name)
#define REDUCT_LIB_ERROR() dlerror()
#endif

#if defined(__GNUC__) || defined(__clang__)
#define REDUCT_LIKELY(_x) __builtin_expect(!!(_x), 1)
#define REDUCT_UNLIKELY(_x) __builtin_expect(!!(_x), 0)
#define REDUCT_NORETURN __attribute__((noreturn))
#define REDUCT_ALWAYS_INLINE __attribute__((always_inline))
#define REDUCT_ALIGNED(_x) __attribute__((aligned(_x)))
#elif defined(_MSC_VER)
#define REDUCT_LIKELY(_x) (_x)
#define REDUCT_UNLIKELY(_x) (_x)
#define REDUCT_NORETURN __declspec(noreturn)
#define REDUCT_ALWAYS_INLINE __forceinline
#define REDUCT_ALIGNED(_x) __declspec(align(_x))
#else
#define REDUCT_LIKELY(_x) (_x)
#define REDUCT_UNLIKELY(_x) (_x)
#define REDUCT_NORETURN
#define REDUCT_ALWAYS_INLINE
#endif

#define REDUCT_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define REDUCT_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))

#define REDUCT_UNUSED(_x) ((void)(_x))

/**
 * @brief PI constant.
 */
#define REDUCT_PI 3.14159265358979323846

/**
 * @brief E constant.
 */
#define REDUCT_E 2.7182818284590452354

/**
 * @brief INFINITY constant.
 */
#define REDUCT_INF \
    (((union { \
        uint64_t u; \
        double f; \
    }){0x7FF0000000000000ULL}) \
            .f)

/**
 * @brief NAN constant.
 */
#define REDUCT_NAN \
    (((union { \
        uint64_t u; \
        double f; \
    }){0x7FF8000000000000ULL}) \
            .f)

/**
 * @brief Maximum path length for Reduct.
 */
#define REDUCT_PATH_MAX 1024

/**
 * @brief Container of macro.
 *
 * Used to get the pointer to a structure from a pointer to one of its members.
 *
 * @param _ptr The pointer to the member.
 * @param _type The type of the structure.
 * @param _member The name of the member.
 */
#define REDUCT_CONTAINER_OF(_ptr, _type, _member) ((_type*)((char*)(_ptr) - offsetof(_type, _member)))

/**
 * @brief Handle type.
 */
typedef struct
{
    uint64_t _value;
} reduct_handle_t;

/**
 * @brief Native function pointer type.
 */
typedef reduct_handle_t (*reduct_native_fn)(struct reduct* reduct, size_t argc, reduct_handle_t* argv);

/**
 * @brief Intrinsic handler function type.
 */
typedef struct reduct_rvsdg_origin* (
    *reduct_native_intrinsic_fn)(struct reduct_builder* builder, struct reduct_list* expr);

/**
 * @brief Module initialization function type.
 */
typedef reduct_handle_t (*reduct_module_init_fn)(struct reduct* reduct);

#define REDUCT_LIB_ENTRY "reduct_module_init" ///< The name of the entry symbol for a Reduct module.

/**
 * @brief Identifies a `reduct_input_t` within a Reduct structure.
 *
 * Avoid the need to store a `reduct_input_t*` within a `reduct_item_t` saving space.
 *
 */
typedef uint16_t reduct_input_id_t;

/**
 * @brief Invalid handle value.
 */
#define REDUCT_INPUT_ID_NONE ((reduct_input_id_t) - 1)

#define REDUCT_ALIGNMENT 64 ///< The memory alignment for items.

#define REDUCT_ROUND_UP(_val, _align) (((_val) + (_align) - 1) & ~((_align) - 1))

#endif
