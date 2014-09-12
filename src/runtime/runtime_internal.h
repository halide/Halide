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
WEAK int64_t halide_current_time_ns(void *user_context);
WEAK void halide_print(void *user_context, const char *msg);
WEAK void halide_error(void *user_context, const char *msg);

char *getenv(const char *);
void free(void *);
void *malloc(size_t);
const char *strstr(const char *, const char *);
int atoi(const char *);
int strcmp(const char* s, const char* t);
int strncmp(const char* s, const char* t, size_t n);
size_t strlen(const char* s);
char *strchr(const char* s, char c);
void* memcpy(void* s1, const void* s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void *memset(void *s, int val, size_t n);
int open(const char *filename, int opts, int mode);
int close(int fd);
ssize_t write(int fd, const void *buf, size_t bytes);
void exit(int);

// Similar to strncpy, but with various non-string arguments. Writes
// arg to dst. Does not write to pointer end or beyond. Returns new
// pointer to end so that calls can be chained.
WEAK char *halide_string_to_string(char *dst, char *end, const char *arg);
WEAK char *halide_double_to_string(char *dst, char *end, double arg, int scientific);
WEAK char *halide_int64_to_string(char *dst, char *end, int64_t arg, int digits);
WEAK char *halide_uint64_to_string(char *dst, char *end, uint64_t arg, int digits);
WEAK char *halide_pointer_to_string(char *dst, char *end, const void *arg);

}

// A convenient namespace for weak functions that are internal to the
// halide runtime.
namespace Halide { namespace Runtime { namespace Internal {

enum Type {BasicPrinter = 0,
           ErrorPrinter = 1,
           StringStreamPrinter = 2};

// A class that can be used like std::cerr for debugging messages from
// the runtime. Dumps items into a stack array, then prints them when
// the object leaves scope using halide_print. Abuses the fact that
// const refs keep objects alive in C++ to defer the print.
template<int type>
class Printer {
public:
    mutable char buf[1024];
    mutable char *dst, *end;
    void *user_context;

    Printer(void *ctx) : dst(buf), end(buf + 1023), user_context(ctx) {
        *end = 0;
    }

    const Printer &operator<<(const char *arg) const {
        dst = halide_string_to_string(dst, end, arg);
        return *this;
    }

    const Printer &operator<<(int64_t arg) const {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    const Printer &operator<<(int32_t arg) const {
        dst = halide_int64_to_string(dst, end, arg, 1);
        return *this;
    }

    const Printer &operator<<(uint64_t arg) const {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    const Printer &operator<<(uint32_t arg) const {
        dst = halide_uint64_to_string(dst, end, arg, 1);
        return *this;
    }

    const Printer &operator<<(double arg) const {
        dst = halide_double_to_string(dst, end, arg, 1);
        return *this;
    }

    const Printer &operator<<(float arg) const {
        dst = halide_double_to_string(dst, end, arg, 0);
        return *this;
    }

    const Printer &operator<<(const void *arg) const {
        dst = halide_pointer_to_string(dst, end, arg);
        return *this;
    }

    // Use it like a stringstream.
    const char *str() {
        return buf;
    }

    ~Printer() {
        if (type == ErrorPrinter) {
            halide_error(user_context, buf);
        } else if (type == BasicPrinter) {
            halide_print(user_context, buf);
        } else {
            // It's a stringstream. Do nothing.
        }
    }
};

// A class that supports << with all the same types as Printer, but does nothing.
class SinkStream {
public:
    SinkStream(void *user_context) {}
};
template<typename T>
SinkStream operator<<(const SinkStream &s, T) {
    return s;
}

typedef Printer<BasicPrinter> print;
typedef Printer<ErrorPrinter> error;
typedef Printer<StringStreamPrinter> stringstream;

#ifdef DEBUG_RUNTIME
typedef Printer<BasicPrinter> debug;
#else
typedef SinkStream debug;
#endif

}}}

using namespace Halide::Runtime::Internal;

#endif

