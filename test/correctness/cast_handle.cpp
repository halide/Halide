#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void check(int r) {
    assert(r == 0);
}

void run_test(bool use_callable) {
    Func f("f"), g("g");
    Var x("x");
    Param<void *> handle("handle");

    f(x) = reinterpret<uint64_t>(handle);

    g(x) = reinterpret<uint64_t>(handle);
    g.vectorize(x, 4);

    int foo = 0;

    Buffer<uint64_t> out1, out2;
    if (!use_callable) {
        handle.set(&foo);

        out1 = f.realize({4});
        out2 = g.realize({4});
    } else {
        // Create a dummy JITUserContext here just to test that
        // passing one here works correctly.
        JITUserContext empty_jit_user_context;

        Callable cf = f.compile_to_callable({handle});
        Callable cg = g.compile_to_callable({handle});

        out1 = Buffer<uint64_t>(4);
        check(cf(&empty_jit_user_context, &foo, out1));

        out2 = Buffer<uint64_t>(4);
        check(cg(&foo, out2));
    }

    uint64_t correct = (uint64_t)((uintptr_t)(&foo));

    for (int x = 0; x < out1.width(); x++) {
        if (out1(x) != correct) {
            printf("out1(%d) = %llu instead of %llu\n",
                   x,
                   (long long unsigned)out1(x),
                   (long long unsigned)correct);
            exit(-1);
        }
        if (out2(x) != correct) {
            printf("out2(%d) = %llu instead of %llu\n",
                   x,
                   (long long unsigned)out2(x),
                   (long long unsigned)correct);
            exit(-1);
        }
    }
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support Param<> for pointer types.\n");
        return 0;
    }
    run_test(false);
    run_test(true);

    printf("Success!\n");
    return 0;
}
