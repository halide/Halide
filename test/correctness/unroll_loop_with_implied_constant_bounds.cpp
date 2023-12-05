#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This test verifies that unrolling/vectorizing is capable of inferring
    // constant bounds of loops that are implied by containing if statement
    // conditions, e.g the following structure should work:

    /*
      let extent = foo
      if (foo == 7) {
        unrolled for (x from 0 to extent) {...}
      }
    */

    for (int i = 0; i < 2; i++) {
        Func intermediate("intermediate");

        Func output1("output1"), output2("output2");

        Var x("x"), y("y"), c("c");

        intermediate(x, y, c) = x + y + c;

        output1(x, y, c) = intermediate(x, y, c);
        output2(x, y, c) = intermediate(x, y, c);

        Expr three_channels =
            (output1.output_buffer().dim(2).extent() == 3 &&
             output1.output_buffer().dim(2).min() == 0 &&
             output2.output_buffer().dim(2).extent() == 3 &&
             output2.output_buffer().dim(2).min() == 0);

        if (i == 0) {
            intermediate.compute_root()
                .specialize(three_channels)
                .unroll(c);
        } else {
            intermediate.compute_root()
                .specialize(three_channels)
                .vectorize(c);
        }

        Pipeline p{{output1, output2}};

        // Should not throw an error in loop unrolling or vectorization.
        p.compile_jit();
    }

    printf("Success!\n");

    return 0;
}
