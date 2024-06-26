#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols

int call_counter[] = {0, 0, 0, 0, 0, 0};
extern "C" HALIDE_EXPORT_SYMBOL int my_func(int counter, int x) {
    call_counter[counter]++;
    return x;
}
HalidePureExtern_2(int, my_func, int, int);

extern "C" HALIDE_EXPORT_SYMBOL int my_impure_func(int counter, int x) {
    call_counter[counter]++;
    return x;
}
HalideExtern_2(int, my_impure_func, int, int);

// A parallel for loop runner that isn't actually parallel
int not_really_parallel_for(JITUserContext *ctx, int (*f)(JITUserContext *, int, uint8_t *), int min, int extent, uint8_t *closure) {
    for (int i = min; i < min + extent; i++) {
        f(ctx, i, closure);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Skipping test for WebAssembly as the wasm JIT cannot support custom parallel runtimes\n");
        return 0;
    }

    Var x, y;
    Func f;

    f(x, y) = my_func(0, Expr(0)) + my_func(1, y) + my_func(2, x * 32 + y);

    // llvm rightly refuses to lift loop invariants out of loops that
    // might have an extent of zero. It's possible wasted work.
    f.bound(x, 0, 32).bound(y, 0, 32);

    Buffer<int> im = f.realize({32, 32});

    // Check the result was what we expected
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int correct = y + 32 * x + y;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                return 1;
            }
        }
    }

    // Check the call counters
    if (call_counter[0] != 1 || call_counter[1] != 32 || call_counter[2] != 32 * 32) {
        printf("Call counters were %d %d %d instead of %d %d %d\n",
               call_counter[0], call_counter[1], call_counter[2],
               1, 32, 32 * 32);
        return 1;
    }

    // Note that pure things get lifted out of loops (even parallel ones), but impure things do not.
    Func g;
    g(x, y) = my_func(3, Expr(0)) + my_impure_func(4, Expr(0));
    g.parallel(y);
    // Avoid the race condition by not actually being parallel
    g.jit_handlers().custom_do_par_for = not_really_parallel_for;
    g.realize({32, 32});

    if (call_counter[3] != 1 || call_counter[4] != 32 * 32) {
        printf("Call counter for parallel call was %d, %d instead of %d, %d\n",
               call_counter[3], call_counter[4], 1, 32 * 32);
        return 1;
    }

    // Check that something we can't compute on the GPU gets lifted
    // out of the GPU loop. This code would fail to link if we didn't
    // do loop invariant code motion.
    if (get_jit_target_from_environment().has_gpu_feature()) {
        Func h;
        h(x, y) = my_func(5, Expr(0));

        Var xi, yi;
        h.gpu_tile(x, y, xi, yi, 8, 8);
        h.realize({32, 32});

        if (call_counter[5] != 1) {
            printf("Call counter for GPU call was %d instead of %d\n",
                   call_counter[5], 1);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
