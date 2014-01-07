#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");

    {
        Func f, g;
        Var x, y;

        // This reduction recursively expands its bounds using
        // subsequent stages.

        ImageParam input(Int(32), 2);

        // This stage gets evaluated over [-2, 101]x[-1, 100]
        f(x, y) = input(x, y);

        // This stage is evaluated over [0, 0]x[-1, 100]
        f(0, y) = f(y-1, y) + f(y+1, y);

        // This stage is evaluated over [0, 99]x[0, 0]
        f(x, 0) = f(x, x-1) + f(x, x+1);

        f.compute_root();

        g(x, y) = f(x, y);

        g.output_buffer().set_bounds(0, 0, 100).set_bounds(1, 0, 100);

        g.infer_input_bounds(100, 100);

        Image<int> in(input.get());
        assert(in.width() == 104 && in.height() == 102);
    }

    // Same thing but with splits to complicate matters.
    {
        Func f, g;

        // This reduction recursively expands its bounds using
        // subsequent stages.

        ImageParam input(Int(32), 2);

        f(x, y) = input(x, y);
        f.unroll(x, 2).unroll(y, 2);

        f(0, y) = f(y-1, y) + f(y+1, y);
        f.update(0).unroll(y, 3);

        f(x, 0) = f(x, x-1) + f(x, x+1);
        f.update(1).unroll(x, 3);

        f.compute_root();

        g(x, y) = f(x, y);

        g.output_buffer().set_bounds(0, 0, 100).set_bounds(1, 0, 100);

        g.infer_input_bounds(100, 100);

        Image<int> in(input.get());
        // The unrolling shouldn't affect anything, because the only
        // thing that reads from the input is the pure step.
        printf("%d %d\n", in.width(), in.height());
        assert(in.width() == 104 && in.height() == 102);
    }

    printf("Success!\n");
    return 0;
}
