#include "HalideRuntime.h"

#include <stdarg.h>
#include <stdint.h>

#define WEAK __attribute__((weak))

typedef void *objc_id;
typedef void *objc_sel;
extern "C" objc_id objc_getClass(const char *name);
extern "C" objc_sel sel_getUid(const char *str);
extern "C" objc_id objc_msgSend(objc_id self, objc_sel op, ...);

extern "C" void NSLogv(objc_id fmt, va_list args);
// To allocate a constant string, use: __builtin___CFStringMakeConstantString

extern "C" {
WEAK int halide_printf(const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);

    // Buy an autorelease pool because this is not perf critical and it is the
    // really safe thing to do.
    objc_id pool =
        objc_msgSend(objc_msgSend(objc_getClass("NSAutoreleasePool"),
                                  sel_getUid("alloc")), sel_getUid("init"));
    objc_id ns_fmt =
        objc_msgSend(objc_msgSend(objc_getClass("NSString"),
                                  sel_getUid("alloc")),
                     sel_getUid("initWithUTF8String:"), fmt);

    NSLogv(ns_fmt, args);

    objc_msgSend(ns_fmt, sel_getUid("release"));

    objc_msgSend(pool, sel_getUid("drain"));

    va_end(args);
    return 1; // TODO: consider changing this routine to be void.
}

extern "C" void *fopen(const char *path, const char *mode);
extern "C" size_t fwrite(const void *ptr, size_t size, size_t n, void *file);
extern "C" int fclose(void *f);

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
