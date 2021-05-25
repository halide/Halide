#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    constexpr int a = 3;
    constexpr int b = 5;

    ImageParam input_a(Int(32), 1, "input");
    Buffer<int> buf_a(1, "input");
    buf_a.fill(a);
    input_a.set(buf_a);

    Buffer<int> input_b(1, "input");
    input_b.fill(b);

    Var x("x");
    Func f("f");
    f(x) = input_a(x) + input_b(x);
    f.compile_jit();

    // Note: the code below should never execute; we expect to fail inside compile_jit().
    // Leaving it here to show what *would* be expected if the Param names did not mismatch/
    //
    // Buffer<int> result(1);
    // f.realize(result);
    // if (result(0) != a + b) {
    //     fprintf(stderr, "Expected to see %d + %d = %d but saw %d\n", a, b, a + b, (int) result(0));
    //     abort();
    // }

    printf("Success!\n");
    return 0;
}
