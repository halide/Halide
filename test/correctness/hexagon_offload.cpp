#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

//    Target target = get_jit_target_from_environment();

    // Pipeline 1 will do input -> host -> dev -> host -> output
    ImageParam in(Int(32), 1);

    Func f, g, out;
    Var x;
    f(x) = in(x) + 1;
    g(x) = f(x) * 2;
    out(x) = g(x) + 3;

    f.compute_root();
    g.compute_root().hexagon(x);
    out.compute_root();

    Image<int> input(1024);
    lambda(x, x * 17 + 83).realize(input);
    in.set(input);

    Image<int> output1(1024);
    out.realize(output1);

    for (int x = 0; x < 1024; x++) {
        int correct = (input(x) + 1) * 2 + 3;
        if (output1(x) != correct) {
            printf("output1(%d) = %d instead of %d\n", x, output1(x), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
