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
#define WEAK __attribute__((weak))

#ifdef BITS_64
#define INT64_C(c)  c ## L
#define UINT64_C(c) c ## UL
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
#endif

#ifdef BITS_32
#define INT64_C(c)  c ## LL
#define UINT64_C(c) c ## ULL
typedef uint32_t uintptr_t;
typedef int32_t intptr_t;
#endif

#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY 0
#define O_RDWR 2

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
int strcmp(const char* s, const char* t);
int strncmp(const char* s, const char* t, size_t n);
size_t strlen(const char* s);
const char *strchr(const char* s, int c);
void* memcpy(void* s1, const void* s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void *memset(void *s, int val, size_t n);
int open(const char *filename, int opts, int mode);
int close(int fd);
ssize_t write(int fd, const void *buf, size_t bytes);
int remove(const char *pathname);
int ioctl(int fd, unsigned long request, ...);
void exit(int);
void abort();
char *strncpy(char *dst, const char *src, size_t n);

// Below are prototypes for various functions called by generated code
// and parts of the runtime but not exposed to users:

// Similar to strncpy, but with various non-string arguments. Writes
// arg to dst. Does not write to pointer end or beyond. Returns
// pointer to one beyond the last character written so that calls can
// be chained.
WEAK char *halide_string_to_string(char *dst, char *end, const char *arg);
WEAK char *halide_double_to_string(char *dst, char *end, double arg, int scientific);
WEAK char *halide_int64_to_string(char *dst, char *end, int64_t arg, int digits);
WEAK char *halide_uint64_to_string(char *dst, char *end, uint64_t arg, int digits);
WEAK char *halide_pointer_to_string(char *dst, char *end, const void *arg);

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

WEAK int halide_device_and_host_malloc(void *user_context, struct buffer_t *buf,
                                       const struct halide_device_interface *device_interface);
WEAK int halide_device_and_host_free(void *user_context, struct buffer_t *buf);

struct halide_filter_metadata_t;
struct _halide_runtime_internal_registered_filter_t {
    // This is a _halide_runtime_internal_registered_filter_t, but
    // recursive types currently break our method that copies types from
    // llvm module to llvm module
    void *next;
    const halide_filter_metadata_t* (*metadata)();
    int (*argv_func)(void **args);
};
WEAK void halide_runtime_internal_register_metadata(_halide_runtime_internal_registered_filter_t *info);

struct mxArray;
WEAK int halide_matlab_call_pipeline(void *user_context,
                                     int (*pipeline)(void **args), const halide_filter_metadata_t *metadata,
                                     int nlhs, mxArray **plhs, int nrhs, const mxArray **prhs);


// Condition variables. Only available on some platforms (those that use the common thread pool).
struct halide_cond {
    uint64_t _private[8];
};

WEAK void halide_cond_init(struct halide_cond *cond);
WEAK void halide_cond_destroy(struct halide_cond *cond);
WEAK void halide_cond_broadcast(struct halide_cond *cond);
WEAK void halide_cond_wait(struct halide_cond *cond, struct halide_mutex *mutex);

}  // extern "C"

/** A macro that calls halide_print if the supplied condition is
 * false, then aborts. Used for unrecoverable errors, or
 * should-never-happen errors. */
#define _halide_stringify(x) #x
#define _halide_expand_and_stringify(x) _halide_stringify(x)
#define halide_assert(user_context, cond)                               \
    if (!(cond)) {                                                      \
        halide_print(user_context, __FILE__ ":" _halide_expand_and_stringify(__LINE__) " Assert failed: " #cond "\n"); \
        abort();                                                        \
    }

// A convenient namespace for weak functions that are internal to the
// halide runtime.
namespace Halide { namespace Runtime { namespace Internal {

extern WEAK void halide_use_jit_module();
extern WEAK void halide_release_jit_module();

// Return a mask with all CPU-specific features supported by the current CPU set.
struct CpuFeatures {
    uint64_t known;     // mask of the CPU features we know how to detect
    uint64_t available; // mask of the CPU features that are available
                              // (always a subset of 'known')
};
extern WEAK CpuFeatures halide_get_cpu_features();

template <typename T>
__attribute__((always_inline)) void swap(T &a, T &b) {
    T t = a;
    a = b;
    b = t;
}

template <typename T>
__attribute__((always_inline)) T max(const T &a, const T &b) {
    return a > b ? a : b;
}

template <typename T>
__attribute__((always_inline)) T min(const T &a, const T &b) {
    return a < b ? a : b;
}

template <typename T, typename U>
__attribute__((always_inline)) T reinterpret(const U &x) {
    T ret;
    memcpy(&ret, &x, min(sizeof(T), sizeof(U)));
    return ret;
}

}}}

using namespace Halide::Runtime::Internal;

#endif
