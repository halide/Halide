#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace Halide;

// clGetPlatformIDs reports the number of platforms available, which can be
// larger than the number of entries the caller asked for. This test installs
// a fake OpenCL library that does exactly that, and checks that the OpenCL
// runtime only uses the platform handles that were actually written.

namespace {

const int max_fake_platforms = 64;
int fake_platforms[max_fake_platforms];
uint32_t platforms_written = 0;

int32_t fake_clGetPlatformIDs(uint32_t num_entries, void **platforms, uint32_t *num_platforms) {
    if (platforms) {
        platforms_written = std::min<uint32_t>(num_entries, max_fake_platforms);
        for (uint32_t i = 0; i < platforms_written; i++) {
            platforms[i] = &fake_platforms[i];
        }
    }
    if (num_platforms) {
        *num_platforms = num_entries + 4;
    }
    return 0;
}

int32_t fake_clGetPlatformInfo(void *platform, uint32_t param_name, size_t value_size, void *value, size_t *value_size_ret) {
    bool valid = false;
    for (uint32_t i = 0; i < platforms_written; i++) {
        valid |= (platform == &fake_platforms[i]);
    }
    if (!valid) {
        fprintf(stderr, "clGetPlatformInfo called with a platform that clGetPlatformIDs did not return: %p\n", platform);
        exit(1);
    }
    const char name[] = "Fake Platform";
    if (value && value_size >= sizeof(name)) {
        memcpy(value, name, sizeof(name));
    }
    if (value_size_ret) {
        *value_size_ret = sizeof(name);
    }
    return 0;
}

int32_t fake_unexpected_call() {
    fprintf(stderr, "Saw an unexpected OpenCL call\n");
    exit(1);
}

int fake_library;

void *my_load_library_impl(const char *name) {
    return &fake_library;
}

void *my_get_library_symbol_impl(void *lib, const char *name) {
    if (!strcmp(name, "clGetPlatformIDs")) {
        return (void *)fake_clGetPlatformIDs;
    } else if (!strcmp(name, "clGetPlatformInfo")) {
        return (void *)fake_clGetPlatformInfo;
    }
    return (void *)fake_unexpected_call;
}

void my_error_handler(JITUserContext *u, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    if (!strstr(msg, "Failed to find platform")) {
        fprintf(stderr, "Saw unexpected err: %s\n", msg);
        exit(1);
    }
    printf("Saw expected err: %s\n", msg);
    printf("Success!\n");
    exit(0);
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        printf("[SKIP] OpenCL not enabled.\n");
        return 0;
    }

    // Ask for a platform that none of the fake platforms match, so that the
    // runtime looks at every platform it thinks it has.
#ifdef _WIN32
    _putenv_s("HL_OCL_PLATFORM_NAME", "No Such Platform");
#else
    setenv("HL_OCL_PLATFORM_NAME", "No Such Platform", 1);
#endif

    JITHandlers handlers;
    handlers.custom_load_library = my_load_library_impl;
    handlers.custom_get_library_symbol = my_get_library_symbol_impl;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Var x, y, xi, yi;
    Func f;
    f(x, y) = cast<int32_t>(x + y);
    f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::OpenCL);
    f.jit_handlers().custom_error = my_error_handler;

    Buffer<int32_t> out = f.realize({64, 64}, target);

    fprintf(stderr, "Should not get here.\n");
    return 1;
}
