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

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)

// A convenient namespace for weak functions that are internal to the
// halide runtime.
namespace Halide { namespace Runtime { namespace Internal {}}}
using namespace Halide::Runtime::Internal;

// Commonly-used extern functions
extern "C" {
int64_t halide_current_time_ns(void *user_context);

char *getenv(const char *);
void free(void *);
void *malloc(size_t);
int snprintf(char *, size_t, const char *, ...);
const char *strstr(const char *, const char *);
int atoi(const char *);
int strcmp(const char* s, const char* t);
int strncmp(const char* s, const char* t, size_t n);
size_t strlen(const char* s);
char *strchr(const char* s, char c);
void* memcpy(void* s1, const void* s2, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void *memset(void *s, int val, size_t n);

}


#endif

