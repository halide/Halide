#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif


int call_counter[4] = {0, 0, 0, 0};
extern "C" DLLEXPORT int my_func(int counter, int x) {
    call_counter[counter]++;
    return x;
}
HalidePureExtern_2(int, my_func, int, int);

// A parallel for loop runner that isn't actually parallel
int not_really_parallel_for(void *ctx, int (*f)(void *, int, uint8_t *), int min, int extent, uint8_t *closure) {
    for (int i = min; i < min + extent; i++) {
        f(ctx, i, closure);
    }
    return 0;
}

int main(int argc, char **argv) {
    Var x, y;
    Func f;

    f(x, y) = my_func(0, Expr(0)) + my_func(1, y) + my_func(2, x*32 + y);

    // llvm rightly refuses to lift loop invariants out of loops that
    // might have an extent of zero. It's possible wasted work.
    f.bound(x, 0, 32).bound(y, 0, 32);

    Buffer<int> im = f.realize(32, 32);

    // Check the result was what we expected
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int correct = y + 32*x + y;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                return -1;
            }
        }
    }

    // Check the call counters
    if (call_counter[0] != 1 || call_counter[1] != 32 || call_counter[2] != 32*32) {
        printf("Call counters were %d %d %d instead of %d %d %d\n",
               call_counter[0], call_counter[1], call_counter[2],
               1, 32, 32*32);
        return -1;
    }

    // Note that things don't get lifted out of parallel loops - Each
    // thread will independently call your extern function.
    Func g;
    g(x, y) = my_func(3, Expr(0));
    g.parallel(y);
    // Avoid the race condition by not actually being parallel
    g.set_custom_do_par_for(&not_really_parallel_for);
    g.realize(32, 32);

    if (call_counter[3] != 32) {
        printf("Call counter for parallel call was %d instead of %d\n",
               call_counter[3], 32);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
