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
extern int fprintf(void *stream, const char *format, ...);

WEAK void __halide_print(void *user_context, const char *str) {
    uint8_t *stdout = __iob_func() + FILE_SIZE;
    //uint8_t *stderr = __iob_func() + FILE_SIZE*2;
    fprintf(stdout, "%s", str);
}

extern int _vsnprintf(char *str, size_t size, const char *, __builtin_va_list ap);

// MSVC doesn't have much of c99
WEAK int snprintf(char *str, size_t size, const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = _vsnprintf(str, size, fmt, args);
    __builtin_va_end(args);
    return ret;
}

WEAK int vsnprintf(char *str, size_t size, const char *fmt, __builtin_va_list ap) {
    return _vsnprintf(str, size, fmt, ap);
}
}
