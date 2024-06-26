#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    ImageParam input(Float(32), 1);

    f(x) = input(cast<int>(ceil(0.3f * ceil(0.4f * floor(x * 22.5f)))));

    f.infer_input_bounds({10});

    Buffer<float> in = input.get();

    int correct = 26;
    if (in.width() != correct) {
        printf("Width is %d instead of %d\n", in.width(), correct);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
