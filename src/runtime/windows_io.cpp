#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

// To get at stderr, we need to know sizeof(FILE) on windows.
#ifdef BITS_64
#define FILE_SIZE 48
#else
#define FILE_SIZE 32
#endif

extern uint8_t *__iob_func();
extern int vfprintf(void *stream, const char *format, __builtin_va_list ap);

WEAK int halide_printf(const char * fmt, ...) {
    uint8_t *stderr = __iob_func() + FILE_SIZE*2;
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    __builtin_va_end(args);
    return ret;
}

}
