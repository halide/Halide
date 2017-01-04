#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// This test exercises the ability to override halide_load_library (etc)
// when using JIT code; to do so, it compiles & calls a simple pipeline
// using a Hexagon schedule, since that is known to load a helper library
// in a well-defined way and is unlikely to change a great deal in the
// near future; if this test breaks because of changes to the Hexagon
// runtime (and the way it uses these calls), we may need to revise
// this test to use a custom library of some sort.

namespace {

uint32_t libhalide_hexagon_host_magic_cookie = 0xdeadbeef;

int load_library_calls = 0;
int get_library_symbol_calls = 0;
int error_calls = 0;

void my_error_handler(void* u, const char *msg) {
    error_calls++;
    printf("Saw error: %s\n", msg);
}

void *my_get_symbol_impl(const char *name) {
    fprintf(stderr, "Saw expected get_symbol: %s\n", name);
    exit(-1);
}

void *my_load_library_impl(const char *name) {
    load_library_calls++;
    if (strcmp(name, "libhalide_hexagon_host.so") != 0) {
        fprintf(stderr, "Saw unexpected call: load_library(%s)\n", name);
        exit(-1);
    }
    // return a well-known non-null pointer.
    return &libhalide_hexagon_host_magic_cookie;
}

void *my_get_library_symbol_impl(void *lib, const char *name) {
    get_library_symbol_calls++;
    if (lib !=  &libhalide_hexagon_host_magic_cookie) {
        fprintf(stderr, "Saw unexpected call: get_library_symbol(%p, %s)\n", lib, name);
        exit(-1);
    }
    return nullptr;
}

}

int main(int argc, char **argv) {
    // These calls are only available for AOT-compiled code:
    //
    //   halide_set_custom_get_symbol(my_get_symbol_impl);
    //   halide_set_custom_load_library(my_load_library_impl);
    //   halide_set_custom_get_library_symbol(my_get_library_symbol_impl);
    //
    // For JIT code, we must use JITSharedRuntime::set_default_handlers().

    Internal::JITHandlers handlers;
    handlers.custom_get_symbol = my_get_symbol_impl;
    handlers.custom_load_library = my_load_library_impl;
    handlers.custom_get_library_symbol = my_get_library_symbol_impl;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Var x("x");
    Func f("f");

    f(x) = cast<int32_t>(x);

    Target target = get_jit_target_from_environment().with_feature(Target::HVX_64);
    f.hexagon().vectorize(x, 32);

    f.set_error_handler(my_error_handler);

    Buffer<int32_t> out = f.realize(64, target);

    assert(load_library_calls == 1);
    assert(get_library_symbol_calls == 1);
    assert(error_calls == 1);

    printf("Success!\n");
    return 0;
}
