#include "HalideRuntime.h"

#include <stdarg.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

extern "C" {

extern char *getenv(const char *);
extern void *fopen(const char *path, const char *mode);
extern size_t fwrite(const void *ptr, size_t size, size_t n, void *file);
extern int vfprintf(void *stream, const char *format, va_list ap);
extern int snprintf(char *str, size_t size, const char *format, ...);
#ifdef __APPLE__
#define stderr __stderrp
#endif
extern void *stderr;

extern int fclose(void *f);

WEAK int halide_printf(const char * fmt, ...) {
    va_list args;
    va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    va_end(args);
    return ret;
}

static bool write_stub(const void *bytes, size_t size, void *f) {
    int count = fwrite(bytes, size, 1, f);
    return (count == 1);
}

WEAK int32_t halide_debug_to_file(const char *filename, uint8_t *data,
				  int32_t s0, int32_t s1, int32_t s2, int32_t s3,
				  int32_t type_code, int32_t bytes_per_element) {
    void *f = fopen(filename, "wb");
    if (!f) return -1;

    int result = halide_write_debug_image(filename, data, s0, s1, s2, s3,
					  type_code, bytes_per_element,
					  write_stub, (void *)f);

    fclose(f);
    return result;
}


}
