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
        assert(in.height() == 102 && in.width() == 104);
    }

    // Same thing but with splits to complicate matters.
    {
        Func f, g;

        // This reduction recursively expands its bounds using
        // subsequent stages.

        ImageParam input(Int(32), 2);

        // This stage gets evaluated over [-2, 104]x[-1, 103]
        f(x, y) = input(x, y);
        f.unroll(x, 2).unroll(y, 2);
        // The region required is [-2, 104]x[-1, 103]. This would be
        // extents of 107 and 105. We don't need to round them up to
        // cope with the unrolls because for pure stages we can just
        // recompute.

        // This stage is evaluated over [0, 0]x[-1, 103]
        f(0, y) = f(y-1, y) + f(y+1, y);
        f.update(0).unroll(y, 3);
        // The range required in y is [-1, 102], but that would be an extent of
        // 104. We need to round it up to 105 due to the unroll.

        // This stage is evaluated over [0, 101]x[0, 0]
        f(x, 0) = f(x, x-1) + f(x, x+1);
        f.update(1).unroll(x, 3);
        // The range required in x is [0, 99], but that would be an extent of
        // 100. We need to round it up to 102 due to the unroll.

        f.compute_root();

        g(x, y) = f(x, y);

        g.output_buffer().set_bounds(0, 0, 100).set_bounds(1, 0, 100);

        g.infer_input_bounds(100, 100);

        Image<int> in(input.get());
        printf("%d %d\n", in.width(), in.height());
        assert(in.width() == 107 && in.height() == 106); // Shouldn't this be 105?
    }

    printf("Success!\n");
    return 0;
}
