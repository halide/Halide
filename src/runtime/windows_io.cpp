#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

// To get at stdout/stderr, we need to know sizeof(FILE) on windows.
#ifdef BITS_64
#define FILE_SIZE 48
#else
#define FILE_SIZE 32
#endif

extern uint8_t *__iob_func();
extern int vfprintf(void *stream, const char *format, __builtin_va_list ap);

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    uint8_t *stdout = __iob_func() + FILE_SIZE;
    //uint8_t *stderr = __iob_func() + FILE_SIZE*2;
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = vfprintf(stdout, fmt, args);
    __builtin_va_end(args);
    return ret;
}

extern int _vsnprintf(char *str, size_t size, const char *, __builtin_va_list ap);

// MSVC doesn't have much of c99
WEAK int snprintf(void *user_context, char *str, size_t size, const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = _vsnprintf(str, size, fmt, args);
    __builtin_va_end(args);
    return ret;
}

}
