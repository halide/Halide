#ifndef HALIDE_RUNTIME_INTERNAL_H
#define HALIDE_RUNTIME_INTERNAL_H

#if __STDC_HOSTED__
#error "Halide runtime files must be compiled with clang in freestanding mode."
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

#define NULL 0

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

// Note that ALWAYS_INLINE should *always* also be `inline`.
#define ALWAYS_INLINE inline __attribute__((always_inline))

// Note that WEAK_INLINE should *not* also be `inline`
#define WEAK_INLINE __attribute__((weak, always_inline))

// --------------

#ifdef BITS_64
#define INT64_C(c) c##L
#define UINT64_C(c) c##UL
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
#endif

#ifdef BITS_32
#define INT64_C(c) c##LL
#define UINT64_C(c) c##ULL
typedef uint32_t uintptr_t;
typedef int32_t intptr_t;
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
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *s, int val, size_t n);
// Use fopen+fileno+fclose instead of open+close - the value of the
// flags passed to open are different on every platform
void *fopen(const char *, const char *);
int fileno(void *);
int fclose(void *);
int close(int);
size_t fwrite(const void *, size_t, size_t, void *);
ssize_t write(int fd, const void *buf, size_t bytes);
int remove(const char *pathname);
int ioctl(int fd, unsigned long request, ...);
char *strncpy(char *dst, const char *src, size_t n);

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
// If lib is NULL, this call should be equivalent to halide_get_symbol(name).
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

struct mxArray;
WEAK int halide_matlab_call_pipeline(void *user_context,
                                     int (*pipeline)(void **args), const halide_filter_metadata_t *metadata,
                                     int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs);

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
};

WEAK void halide_use_jit_module();
WEAK void halide_release_jit_module();

WEAK_INLINE int halide_malloc_alignment();
WEAK_INLINE void halide_abort();

void halide_thread_yield();

}  // extern "C"

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

template<typename T, typename U>
ALWAYS_INLINE T reinterpret(const U &x) {
    T ret;
    memcpy(&ret, &x, min(sizeof(T), sizeof(U)));
    return ret;
}
}  // namespace

// A namespace for runtime modules to store their internal state
// in. Should not be for things communicated between runtime modules,
// because it's possible for them to be compiled with different c++
// name mangling due to mixing and matching target triples.
namespace Halide {
namespace Runtime {
namespace Internal {
// Empty
}
}  // namespace Runtime
}  // namespace Halide
using namespace Halide::Runtime::Internal;

/** A macro that calls halide_print if the supplied condition is
 * false, then aborts. Used for unrecoverable errors, or
 * should-never-happen errors. */
#define _halide_stringify(x) #x
#define _halide_expand_and_stringify(x) _halide_stringify(x)
#define halide_assert(user_context, cond)                                                                                  \
    do {                                                                                                                   \
        if (!(cond)) {                                                                                                     \
            halide_print(user_context, __FILE__ ":" _halide_expand_and_stringify(__LINE__) " Assert failed: " #cond "\n"); \
            halide_abort();                                                                                                \
        }                                                                                                                  \
    } while (0)

#endif
