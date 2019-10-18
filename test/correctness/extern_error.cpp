#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

bool extern_error_called = false;
extern "C" DLLEXPORT
int extern_error(void *user_context, halide_buffer_t *out) {
    extern_error_called = true;
    return -1;
}

bool error_occurred = false;
extern "C" DLLEXPORT
void my_halide_error(void *user_context, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("Skipping test for WebAssembly as the wasm JIT cannot support passing arbitrary pointers to/from HalideExtern code.\n");
        return 0;
    }

    std::vector<ExternFuncArgument> args;
    args.push_back(user_context_value());

    Func f;
    f.define_extern("extern_error", args, Float(32), 1);
    f.set_error_handler(&my_halide_error);
    f.realize(100);

    if (!error_occurred || !extern_error_called) {
        printf("There was supposed to be an error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
