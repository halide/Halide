#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// This test exercises the ability to override halide_get_library_symbol (etc)
// when using JIT code; to do so, it compiles & calls a simple pipeline
// using an OpenCL schedule, since that is known to use these calls
// in a (reasonably) well-defined way and is unlikely to change a great deal
// in the near future; additionally, it doesn't require a particular
// feature in LLVM (unlike, say, Hexagon).

namespace {

int load_library_calls = 0;
int get_library_symbol_calls = 0;

void my_error_handler(JITUserContext *u, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    if (!strstr(msg, "OpenCL API not found")) {
        fprintf(stderr, "Saw unexpected err: %s\n", msg);
        exit(1);
    }
    printf("Saw expected err: %s\n", msg);
    if (load_library_calls == 0 || get_library_symbol_calls == 0) {
        fprintf(stderr, "Should have seen load_library and get_library_symbol calls!\n");
        exit(1);
    }
    printf("Success!\n");
    exit(0);
}

void *my_get_symbol_impl(const char *name) {
    fprintf(stderr, "Saw unexpected call: get_symbol(%s)\n", name);
    exit(1);
}

void *my_load_library_impl(const char *name) {
    load_library_calls++;
    if (!strstr(name, "OpenCL") && !strstr(name, "opencl")) {
        fprintf(stderr, "Saw unexpected call: load_library(%s)\n", name);
        exit(1);
    }
    printf("Saw load_library: %s\n", name);
    return nullptr;
}

void *my_get_library_symbol_impl(void *lib, const char *name) {
    get_library_symbol_calls++;
    if (lib != nullptr || strcmp(name, "clGetPlatformIDs") != 0) {
        fprintf(stderr, "Saw unexpected call: get_library_symbol(%p, %s)\n", lib, name);
        exit(1);
    }
    printf("Saw get_library_symbol: %s\n", name);
    return nullptr;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        printf("[SKIP] OpenCL not enabled.\n");
        return 0;
    }

    // These calls are only available for AOT-compiled code:
    //
    //   halide_set_custom_get_symbol(my_get_symbol_impl);
    //   halide_set_custom_load_library(my_load_library_impl);
    //   halide_set_custom_get_library_symbol(my_get_library_symbol_impl);
    //
    // For JIT code, we must use JITSharedRuntime::set_default_handlers().

    JITHandlers handlers;
    handlers.custom_get_symbol = my_get_symbol_impl;
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
