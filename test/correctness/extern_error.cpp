#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool extern_error_called = false;
extern "C" HALIDE_EXPORT_SYMBOL int extern_error(JITUserContext *user_context, halide_buffer_t *out) {
    extern_error_called = true;
    return halide_error_code_generic_error;
}

bool error_occurred = false;
extern "C" HALIDE_EXPORT_SYMBOL void my_halide_error(JITUserContext *user_context, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.\n");
        return 0;
    }

    std::vector<ExternFuncArgument> args;
    args.push_back(user_context_value());

    Func f;
    f.define_extern("extern_error", args, Float(32), 1);
    f.jit_handlers().custom_error = my_halide_error;
    f.realize({100});

    if (!error_occurred || !extern_error_called) {
        printf("There was supposed to be an error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
