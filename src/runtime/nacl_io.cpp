#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

// NaCl can run with either glibc or newlib; under newlib, stderr and friends
// are offsets into a thread-context pointer, not a single global that we
// can declare extern. To work reasonably in both environments, we use
// vsnprintf() to write to a local buffer, then write() to fd=2.
extern size_t write(int fd, const void *buf, size_t count);
extern int vsnprintf(char *buf, size_t buf_size, const char *format, __builtin_va_list ap);
const int STDERR_FILENO = 2;

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    const size_t BUF_SIZE = 1024;
    char buf[BUF_SIZE];
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = vsnprintf(buf, BUF_SIZE, fmt, args);
    __builtin_va_end(args);
    size_t len = 0;
    while (len < BUF_SIZE-1 && buf[len] != 0) ++len;
    write(STDERR_FILENO, buf, len);
    return ret;
}

}
