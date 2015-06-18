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
void exit(int);
char *strncpy(char *dst, const char *src, size_t n);

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

#ifdef DEBUG_RUNTIME
WEAK int halide_start_clock(void *user_context);
WEAK int64_t halide_current_time_ns(void *user_context);
#endif
}

// A convenient namespace for weak functions that are internal to the
// halide runtime.
namespace Halide { namespace Runtime { namespace Internal {

enum PrinterType {BasicPrinter = 0,
                  ErrorPrinter = 1,
                  StringStreamPrinter = 2};

// A class for constructing debug messages from the runtime. Dumps
// items into a stack array, then prints them when the object leaves
// scope using halide_print. Think of it as a stringstream that prints
// when it dies. Use it like this:

// debug(user_context) << "A" << b << c << "\n";

// If you use it like this:

// debug d(user_context);
// d << "A";
// d << b;
// d << c << "\n";

// Then remember the print only happens when the debug object leaves
// scope, which may print at a confusing time.

namespace {
template<int type, uint64_t length = 1024>
class Printer {
public:
    char *buf, *dst, *end;
    void *user_context;
    bool own_mem;

    Printer(void *ctx, char *mem = NULL) : user_context(ctx), own_mem(mem == NULL) {
        buf = mem ? mem : (char *)halide_malloc(user_context, length);
        dst = buf;
        end = buf + (length-1);
        *end = 0;
    }

    Printer &operator<<(const char *arg) {
        dst = halide_string_to_string(dst, end, arg);
        return *this;
    }

    Printer &operator<<(int64_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(int32_t arg) {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(uint64_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(uint32_t arg) {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(double arg) {
        dst = halide_double_to_string(dst, end, arg, 1);
        return *this;
    }

    Printer &operator<<(float arg) {
        dst = halide_double_to_string(dst, end, arg, 0);
        return *this;
    }

    Printer &operator<<(const void *arg) {
        dst = halide_pointer_to_string(dst, end, arg);
        return *this;
    }

    // Use it like a stringstream.
    const char *str() {
        return buf;
    }

    // Clear it. Useful for reusing a stringstream.
    void clear() {
        dst = buf;
        dst[0] = 0;
    }

    // Returns the number of characters in the buffer
    uint64_t size() const {
        return (uint64_t)(dst-buf);
    }

    ~Printer() {
        if (type == ErrorPrinter) {
            halide_error(user_context, buf);
        } else if (type == BasicPrinter) {
            halide_print(user_context, buf);
        } else {
            // It's a stringstream. Do nothing.
        }
        if (own_mem) halide_free(user_context, buf);
    }
};

// A class that supports << with all the same types as Printer, but
// does nothing and should compile to a no-op.
class SinkPrinter {
public:
    SinkPrinter(void *user_context) {}
};
template<typename T>
SinkPrinter operator<<(const SinkPrinter &s, T) {
    return s;
}

typedef Printer<BasicPrinter> print;
typedef Printer<ErrorPrinter> error;
typedef Printer<StringStreamPrinter> stringstream;

#ifdef DEBUG_RUNTIME
typedef Printer<BasicPrinter> debug;
#else
typedef SinkPrinter debug;
#endif
}

extern WEAK void halide_use_jit_module();
extern WEAK void halide_release_jit_module();

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

}}}

using namespace Halide::Runtime::Internal;

#endif
