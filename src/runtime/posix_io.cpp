#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

extern "C" {
WEAK int halide_printf(const char * fmt, ...) {
    va_list args;
    va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    va_end(args);
    return ret;
}

static bool write_stub(const void *bytes, size_t size, void *f) {
    int count = fwrite(bytes, size, 1, (FILE *)f);
    return (count == 1);
}

WEAK int32_t halide_debug_to_file(const char *filename, uint8_t *data, 
				  int32_t s0, int32_t s1, int32_t s2, int32_t s3, 
				  int32_t type_code, int32_t bytes_per_element) {
    size_t written;
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;

    int result = halide_write_debug_image(filename, data, s0, s1, s2, s3,
					  type_code, bytes_per_element,
					  write_stub, (void *)f);

    fclose(f);
    return result;
}

}
