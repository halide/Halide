#include <stdarg.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

extern "C" {

extern void __android_log_vprint(int, const char *, const char *, va_list);

extern uint32_t fwrite(void *, uint32_t, uint32_t, void *);
extern void *fopen(const char *, const char *);
extern void fclose(void *);

WEAK int halide_printf(const char * fmt, ...) {
    va_list args;
    va_start(args,fmt);
    __android_log_vprint(7, "halide", fmt, args);
    va_end(args);
    return 0;
}

WEAK int32_t halide_debug_to_file(const char *filename, uint8_t *data, 
                                  int32_t s0, int32_t s1, int32_t s2, int32_t s3, 
                                  int32_t type_code, int32_t bytes_per_element) {
    void *f = fopen(filename, "wb");
    if (!f) return -1;
    uint32_t elts = s0;
    elts *= s1*s2*s3;
    int32_t header[] = {s0, s1, s2, s3, type_code};
    uint32_t written = fwrite((void *)(&header[0]), 4, 5, f);
    if (written != 5) return -2;
    written = fwrite((void *)data, bytes_per_element, elts, f);    
    fclose(f);
    if (written == elts) return 0;
    else return int(written)+1;
}

}
