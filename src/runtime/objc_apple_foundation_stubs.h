#ifndef HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H
#define HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H

extern "C" {

extern void *halide_ns_create_autorelease_pool();
extern void halide_ns_release_and_free_autorelease_pool(void *pool);

void halide_ns_log_utf8_string(const char *str);

}

#endif // HALIDE_OBJC_APPLE_FOUNDATION_STUBS_H



