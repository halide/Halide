#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_counter = 0;
extern "C" DLLEXPORT float my_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, my_func, int, float);

void run_test(bool use_callable) {
    call_counter = 0;

    std::vector<ExternFuncArgument> args;
    args.push_back(user_context_value());

    Var x, y;
    Func monitor;
    monitor(x, y) = my_func(x, cast<float>(y));

    Func f;
    f.define_extern("extern_func", args, Float(32), 2);

    Pipeline p(f);
    p.set_jit_externs({{"extern_func", JITExtern{monitor}}});

    Buffer<float> imf;
    if (!use_callable) {
        imf = p.realize({32, 32});
    } else {
        Callable c = p.compile_to_callable({});

        // Changing g's jit_externs shouldn't affect any existing Callables
        p.set_jit_externs({});

        imf = Buffer<float>(32, 32);
        int r = c(imf);
        _halide_user_assert(r == 0);
    }

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i * j);
            float delta = imf(i, j) - correct;
            if (delta < -0.001 || delta > 0.001) {
                printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                exit(-1);
            }
        }
    }

    if (call_counter != 32 * 32) {
        printf("In pipeline_set_jit_externs_func, my_func was called %d times instead of %d\n", call_counter, 32 * 32);
        exit(-1);
    }
}

int main(int argc, char **argv) {
    // set_jit_externs() implicitly adds a user_context arg to the externs, which
    // we can't yet support. TODO: this actually should work, but doesn't.
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.\n");
        return 0;
    }

    run_test(false);
    run_test(true);

    printf("Success!\n");
    return 0;
}
