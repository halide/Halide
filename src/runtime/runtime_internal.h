#ifndef HALIDE_RUNTIME_INTERNAL_H
#define HALIDE_RUNTIME_INTERNAL_H

#ifdef COMPILING_HALIDE_RUNTIME_TESTS
// Only allowed if building Halide runtime tests ... since they use system compiler which may be GCC or MSVS
#else
#if __STDC_HOSTED__
#error "Halide runtime files must be compiled with clang in freestanding mode."
#endif
#endif

#ifdef __UINT8_TYPE__
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT8_TYPE__ int8_t;
typedef __UINT8_TYPE__ uint8_t;
#else
typedef signed __INT64_TYPE__ int64_t;
typedef unsigned __INT64_TYPE__ uint64_t;
typedef signed __INT32_TYPE__ int32_t;
typedef unsigned __INT32_TYPE__ uint32_t;
typedef signed __INT16_TYPE__ int16_t;
typedef unsigned __INT16_TYPE__ uint16_t;
typedef signed __INT8_TYPE__ int8_t;
typedef unsigned __INT8_TYPE__ uint8_t;
#endif
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

typedef ptrdiff_t ssize_t;

// --------------

// In Halide runtime code, most functions should just be WEAK, whether or not
// they're part of the public API.
//
// ALWAYS_INLINE is for things that either should be inlined for performance
// reasons, or for things that go into every compiled pipeline (not just the
// standalone runtime), as those things have to either disappear entirely by
// being inlined away, or have "inline" linkage to avoid multiple definition
// errors on platforms that have no weak linkage.
//
// WEAK_INLINED is a special case where we are 'inlining' the bitcode at
// bitcode-compilation time (rather than C++ compilation time); it's needed
// for a few places in the runtime where we can't inline in the traditional
// way.

#define WEAK __attribute__((weak))

#define NEVER_INLINE __attribute__((noinline))

// Note that ALWAYS_INLINE should *always* also be `inline`.
#define ALWAYS_INLINE inline __attribute__((always_inline))

// Note that WEAK_INLINE should *not* also be `inline`
#define WEAK_INLINE __attribute__((weak, always_inline))

// --------------

#ifdef BITS_64
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
#endif

#ifdef BITS_32
typedef uint32_t uintptr_t;
typedef int32_t intptr_t;
#endif

#if !defined(BITS_32) && !defined(BITS_64)
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;
static_assert(sizeof(uintptr_t) == sizeof(void *));
static_assert(sizeof(intptr_t) == sizeof(void *));
#endif

#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Commonly-used extern functions
extern "C" {
void *halide_malloc(void *user_context, size_t x);
void halide_free(void *user_context, void *ptr);
WEAK int64_t halide_current_time_ns(void *user_context);
WEAK void halide_print(void *user_context, const char *msg);
WEAK void halide_error(void *user_context, const char *msg);
WEAK void (*halide_set_custom_print(void (*print)(void *, const char *)))(void *, const char *);
WEAK void (*halide_set_error_handler(void (*handler)(void *, const char *)))(void *, const char *);

char *getenv(const char *);
void free(void *);
void *malloc(size_t);
const char *strstr(const char *, const char *);
int atoi(const char *);
int strcmp(const char *s, const char *t);
int strncmp(const char *s, const char *t, size_t n);
size_t strlen(const char *s);
const char *strchr(const char *s, int c);
void *memcpy(void *s1, const void *s2, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *s, int val, size_t n);

// No: don't call fopen() directly; some platforms may want to require
// use of other calls instead, so you should bottleneck all calls to fopen()
// to halide_fopen() instead, which allows for link-time overriding.
//
// Use fopen+fileno+fclose instead of open+close - the value of the
// flags passed to open are different on every platform
//
// void *fopen(const char *, const char *);

WEAK_INLINE void *halide_fopen(const char *filename, const char *type);

int fileno(void *);
int fclose(void *);
int close(int);
size_t fwrite(const void *, size_t, size_t, void *);
ssize_t write(int fd, const void *buf, size_t bytes);
int remove(const char *pathname);
int ioctl(int fd, unsigned long request, ...);
char *strncpy(char *dst, const char *src, size_t n);
void abort();

// Below are prototypes for various functions called by generated code
// and parts of the runtime but not exposed to users:

// Similar to strncpy, but with various non-string arguments. Writes
// arg to dst. Does not write to pointer end or beyond. Returns
// pointer to one beyond the last character written so that calls can
// be chained.

struct halide_buffer_t;
struct halide_type_t;
WEAK char *halide_string_to_string(char *dst, char *end, const char *arg);
WEAK char *halide_double_to_string(char *dst, char *end, double arg, int scientific);
WEAK char *halide_int64_to_string(char *dst, char *end, int64_t arg, int digits);
WEAK char *halide_uint64_to_string(char *dst, char *end, uint64_t arg, int digits);
WEAK char *halide_pointer_to_string(char *dst, char *end, const void *arg);
WEAK char *halide_buffer_to_string(char *dst, char *end, const halide_buffer_t *arg);
WEAK char *halide_type_to_string(char *dst, char *end, const halide_type_t *arg);

// Search the current process for a symbol with the given name.
WEAK void *halide_get_symbol(const char *name);
// Platform specific implementations of dlopen/dlsym.
WEAK void *halide_load_library(const char *name);
// If lib is nullptr, this call should be equivalent to halide_get_symbol(name).
WEAK void *halide_get_library_symbol(void *lib, const char *name);

WEAK int halide_start_clock(void *user_context);
WEAK int64_t halide_current_time_ns(void *user_context);
WEAK void halide_sleep_ms(void *user_context, int ms);
WEAK void halide_device_free_as_destructor(void *user_context, void *obj);
WEAK void halide_device_and_host_free_as_destructor(void *user_context, void *obj);
WEAK void halide_device_host_nop_free(void *user_context, void *obj);

// The pipeline_state is declared as void* type since halide_profiler_pipeline_stats
// is defined inside HalideRuntime.h which includes this header file.
WEAK void halide_profiler_stack_peak_update(void *user_context,
                                            void *pipeline_state,
                                            uint64_t *f_values);
WEAK void halide_profiler_memory_allocate(void *user_context,
                                          void *pipeline_state,
                                          int func_id,
                                          uint64_t incr);
WEAK void halide_profiler_memory_free(void *user_context,
                                      void *pipeline_state,
                                      int func_id,
                                      uint64_t decr);
WEAK int halide_profiler_pipeline_start(void *user_context,
                                        const char *pipeline_name,
                                        int num_funcs,
                                        const uint64_t *func_names);
WEAK int halide_host_cpu_count();

WEAK int halide_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf,
                                       const struct halide_device_interface_t *device_interface);
WEAK int halide_device_and_host_free(void *user_context, struct halide_buffer_t *buf);

struct halide_filter_metadata_t;

WEAK int halide_trace_helper(void *user_context,
                             const char *func,
                             void *value, int *coords,
                             int type_code, int type_bits, int type_lanes,
                             int code,
                             int parent_id, int value_index, int dimensions,
                             const char *trace_tag);

struct halide_pseudostack_slot_t {
    void *ptr;
    size_t size;
    size_t cumulative_size;
};

WEAK void halide_use_jit_module();
WEAK void halide_release_jit_module();

// These are all intended to be inlined into other pieces of runtime code;
// they are not intended to be called or replaced by user code.
WEAK_INLINE int halide_internal_malloc_alignment();
WEAK_INLINE void *halide_internal_aligned_alloc(size_t alignment, size_t size);
WEAK_INLINE void halide_internal_aligned_free(void *ptr);

void halide_thread_yield();

}  // extern "C"

template<typename T>
ALWAYS_INLINE T align_up(T p, size_t alignment) {
    return (p + alignment - 1) & ~(alignment - 1);
}

template<typename T>
ALWAYS_INLINE T is_power_of_two(T value) {
    return (value != 0) && ((value & (value - 1)) == 0);
}

namespace {
template<typename T>
ALWAYS_INLINE void swap(T &a, T &b) {
    T t = a;
    a = b;
    b = t;
}

template<typename T>
ALWAYS_INLINE T max(const T &a, const T &b) {
    return a > b ? a : b;
}

template<typename T>
ALWAYS_INLINE T min(const T &a, const T &b) {
    return a < b ? a : b;
}

}  // namespace

// A namespace for runtime modules to store their internal state
// in. Should not be for things communicated between runtime modules,
// because it's possible for them to be compiled with different c++
// name mangling due to mixing and matching target triples (this usually
// only affects Windows builds).
namespace Halide {
namespace Runtime {
namespace Internal {
// Empty
}
}  // namespace Runtime
}  // namespace Halide
using namespace Halide::Runtime::Internal;

/** halide_abort_if_false() is a macro that calls halide_print if the supplied condition is
 * false, then aborts. Used for unrecoverable errors, or should-never-happen errors.
 *
 * Note that this is *NOT* a debug-only macro;
 * the condition will be checked in *all* build modes! */
#define _halide_stringify(x) #x
#define _halide_expand_and_stringify(x) _halide_stringify(x)
#define halide_abort_if_false(user_context, cond)                                                                                           \
    do {                                                                                                                                    \
        if (!(cond)) {                                                                                                                      \
            halide_print(user_context, __FILE__ ":" _halide_expand_and_stringify(__LINE__) " halide_abort_if_false() failed: " #cond "\n"); \
            abort();                                                                                                                        \
        }                                                                                                                                   \
    } while (0)

/** halide_debug_assert() is like halide_assert(), but only expands into a check when
 * DEBUG_RUNTIME is defined. It is what you want to use in almost all cases. */
#ifdef DEBUG_RUNTIME
#define halide_debug_assert(user_context, cond)                                                                                           \
    do {                                                                                                                                  \
        if (!(cond)) {                                                                                                                    \
            halide_print(user_context, __FILE__ ":" _halide_expand_and_stringify(__LINE__) " halide_debug_assert() failed: " #cond "\n"); \
            abort();                                                                                                                      \
        }                                                                                                                                 \
    } while (0)
#else
#define halide_debug_assert(user_context, cond)
#endif

#endif  // HALIDE_RUNTIME_INTERNAL_H
