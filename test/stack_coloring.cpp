#include <Halide.h>
#include <stdio.h>

#ifdef __linux__
#include <sys/resource.h>
#endif

using namespace Halide;

int main(int argc, char **argv) {
    
    // Define a fibonacci-like pipeline, where each stage depends on
    // the two previous stages.
    Func f[100];

    // Use a seed parameter to prevent constant folding the whole pipeline.
    Param<int> seed;
    Var x;
    f[0](x) = seed + x;
    f[0].compute_root();
    f[1](x) = seed + x;
    f[1].compute_root();
    for (int i = 2; i < 100; i++) {
        // Grow and shrink the domain at each iteration to test the
        // best-fit algorithm. Generally the domain gets larger.
        int min = (rand() & 15) - i;
        int max = (100 - min) + i;
        Expr clamped_x = clamp(x*2 - 50, min, max);
        f[i](x) = f[i-1](clamped_x) + f[i-2](clamped_x);
        f[i].compute_root();
    }

    // This lets everything be a stack allocation (of 8000 bytes each)
    Func stack;
    stack(x) = f[99](x);
    stack.bound(x, 0, 2000);

    // Also compile a heap version for comparison
    Func heap;
    heap(x) = f[99](x);
    heap.bound(x, 0, 20000);

    seed.set(1);

    // If we don't reuse stack space, this should use 800K of stack. If we do, it should use 24K.

    // On linux we can force a crash above a certain stack
    // size. Before we do, we'd better jit compile, because
    // compilation uses a lot of stack space.

    #ifdef __linux__
    stack.compile_jit();
    heap.compile_jit();
    rlimit lim = {50000, 50000};
    setrlimit(RLIMIT_STACK, &lim);
    perror("setrlimit");
    #endif
    
    Image<int> result = stack.realize(2000);
    Image<int> correct = heap.realize(20000);

    for (int i = 0; i < 2000; i++) {
        if (result(i) != correct(i)) {
            printf("Disagreement at %d: heap = %d, stack = %d\n", i, correct(i), result(i));
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}

