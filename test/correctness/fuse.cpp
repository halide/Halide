#include "Halide.h"
#include "halide_test_dirs.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    {
        Func f, g;

        Expr e = x * 3 + y;
        f(x, y) = e;
        g(x, y) = e;

        f.compute_root();
        Var xi("xi"), xo("xo"), yi("yi"), yo("yo"), fused("fused");

        // Let's try a really complicated schedule that uses split,
        // reorder, and fuse.  Tile g, then fuse the tile indices into a
        // single var, and fuse the within tile indices into a single var,
        // then tile those two vars again, and do the same fusion
        // again. Neither of the tilings divide the region we're going to
        // evaluate. Finally, vectorize across the resulting y dimension,
        // whatever that means.

        g.compute_root()
            .tile(x, y, xo, yo, xi, yi, 3, 5)
            .fuse(xo, yo, y)
            .fuse(xi, yi, x)
            .tile(x, y, xo, yo, xi, yi, 7, 6)
            .fuse(xo, yo, y)
            .fuse(xi, yi, x)
            .vectorize(y, 4);

        RDom r(-16, 32, -16, 32);
        Func error;
        error() = maximum(abs(f(r.x, r.y) - g(r.x, r.y)));

        int err = evaluate_may_gpu<uint32_t>(error());
        if (err != 0) {
            printf("Fusion caused a difference in the output\n");
            return -1;
        }
    }

    {
        ImageParam p(Int(32), 2);
        Func f;

        f(x, y) = p(x, y);

        // To make x and y fuse cleanly, we need to know the min of the inner
        // fuse dimension is 0.
        f.output_buffer().dim(0).set_min(0);
        p.dim(0).set_min(0);
        // And that the stride of dim 1 is equal to the extent of dim 0.
        f.output_buffer().dim(1).set_stride(f.output_buffer().dim(0).extent());
        p.dim(1).set_stride(f.output_buffer().dim(0).extent());

        // Fuse and vectorize x and y.
        Var xy("xy");
        f.compute_root()
            .fuse(x, y, xy)
            .vectorize(xy, 16);

        // Compile the program to a stmt.
        std::string result_file = Internal::get_test_tmp_dir() + "fuse.stmt";
        Internal::ensure_no_file_exists(result_file);
        f.compile_to_lowered_stmt(result_file, f.infer_arguments());

        // Verify that the compiled stmt does not contain a mod operator.
        std::vector<char> stmt = Internal::read_entire_file(result_file);
        if (std::find(stmt.begin(), stmt.end(), '%') != stmt.end()) {
            printf("Fused schedule contained an unfused ramp: %s", stmt.data());
            return -1;
        }
    }
    printf("Success!\n");
    return 0;
}
