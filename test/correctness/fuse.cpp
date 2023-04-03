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
            return 1;
        }
    }

    class CheckForMod : public Internal::IRMutator {
        using IRMutator::visit;

        Expr visit(const Internal::Mod *op) override {
            std::cerr << "Found mod: " << Expr(op) << "\n";
            exit(1);
            return op;
        }
    };

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

        f.add_custom_lowering_pass(new CheckForMod);
        f.compile_jit();
    }

    // Test two cases where the fuse arithmetic should vanish due to nested vectorization

    // The first case should turn into a sum of slices of a vector
    {
        ImageParam p(Int(32), 2);
        RDom r(0, 2);
        Func f;

        f(x) += p(x, r);

        f.output_buffer().dim(0).set_bounds(0, 8);
        p.dim(0).set_bounds(0, 8);
        p.dim(1).set_stride(8);

        // Fuse and vectorize x and y.
        RVar rx;
        f.compute_root()
            .update()
            .reorder(x, r)  // x is inside r, so this is a sum of slices
            .fuse(x, r, rx)
            .atomic()
            .vectorize(rx);

        f.add_custom_lowering_pass(new CheckForMod);
        f.compile_jit();
    }

    // The second case should turn into a vector reduce instruction, with no modulo in the indexing
    {
        ImageParam p(Int(32), 2);
        RDom r(0, 2);
        Func f;

        f(x) += p(x, r);

        f.output_buffer().dim(0).set_bounds(0, 8);
        p.dim(0).set_bounds(0, 8);
        p.dim(1).set_stride(8);

        // Fuse and vectorize x and y.
        RVar rx;
        f.compute_root()
            .update()
            .reorder(r, x)
            .fuse(r, x, rx)  // r is inside x, so this is a vector reduce
            .atomic()
            .vectorize(rx);

        f.add_custom_lowering_pass(new CheckForMod);
        f.compile_jit();
    }
    printf("Success!\n");
    return 0;
}
