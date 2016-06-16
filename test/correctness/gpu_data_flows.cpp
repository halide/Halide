#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Target target = get_jit_target_from_environment();

    // We want to test all possible data flows for a buffer:

    // input -> host
    // input -> dev
    // host -> host
    // host -> dev
    // dev -> host
    // dev -> dev
    // dev -> output
    // host -> output

    // We can't really test the last two in the same routine, so we'll
    // run two routines.

    {
        // Pipeline 1 will do input -> host -> dev -> host -> output
        ImageParam in(Int(32), 1);

        Func f, g, out;
        Var x;
        f(x) = in(x) + 1;
        g(x) = f(x) * 2;
        out(x) = g(x) + 3;

        f.compute_root();
        if (target.has_gpu_feature()) {
            g.compute_root().gpu_tile(x, 16);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            g.compute_root().hexagon();
        }
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
    }


    {
        // Pipeline 2 will do input -> dev -> dev -> output
        ImageParam in(Int(32), 1);
        Func f, out;
        Var x;
        f(x) = in(x) + 1;
        out(x) = f(x) * 2;

        if (target.has_gpu_feature()) {
            f.compute_root().gpu_tile(x, 16);
            out.compute_root().gpu_tile(x, 16);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
            f.compute_root().hexagon();
            out.compute_root().hexagon();
        }

        Image<int> input(1024);
        lambda(x, x * 17 + 83).realize(input);
        in.set(input);

        Image<int> output2(1024);
        out.realize(output2);

        for (int x = 0; x < 1024; x++) {
            int correct = (input(x) + 1) * 2;
            if (output2(x) != correct) {
                printf("output2(%d) = %d instead of %d\n", x, output2(x), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
