#include "runtime_internal.h"
#include <stdarg.h>

extern "C" {

// The tracing module needs open, write, close
typedef ptrdiff_t ssize_t;
extern ssize_t _write(int fd, const void *buf, size_t count);
extern int _close(int fd);
extern int _open(const char *filename, int opts, int mode);
ssize_t write(int fd, const void *buf, size_t count) {return _write(fd, buf, count);}
int close(int fd) {return _close(fd);}
int open(const char *filename, int opts, int mode) {return _open(filename, opts, mode);}

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

extern int _vsnprintf(char *str, size_t size, const char *, va_list ap);

// MSVC doesn't have much of c99
WEAK int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args,fmt);
    int ret = _vsnprintf(str, size, fmt, args);
    va_end(args);
    return ret;
}

WEAK int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return _vsnprintf(str, size, fmt, ap);
}
}
