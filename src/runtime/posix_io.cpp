#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern char *getenv(const char *);
extern size_t fwrite(const void *ptr, size_t size, size_t n, void *file);
extern int vfprintf(void *stream, const char *format, __builtin_va_list ap);
extern int snprintf(char *str, size_t size, const char *format, ...);

#ifdef HALIDE_OS_OSX
#define stderr __stderrp
#endif
extern void *stderr;

WEAK int halide_printf(const char * fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    __builtin_va_end(args);
    return ret;
}

}
