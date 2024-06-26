#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<int> input(100, 50);

    // This image represents the range [100, 199]*[50, 99]
    input.set_min(100, 50);

    input(100, 50) = 123;
    input(198, 99) = 234;

    Func f;
    Var x, y;
    f(x, y) = input(2 * x, y / 2);

    f.compile_jit();

    // The output will represent the range from [50, 99]*[100, 199]
    Buffer<int> result(50, 100);
    result.set_min(50, 100);

    f.realize(result);

    if (result(50, 100) != 123 || result(99, 199) != 234) {
        fprintf(stderr, "Err: f(50, 100) = %d (supposed to be 123)\n"
                        "f(99, 199) = %d (supposed to be 234)\n",
                result(50, 100), result(99, 199));
        return 1;
    }

    printf("Success!\n");

    return 0;
}
