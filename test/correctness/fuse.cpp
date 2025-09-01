#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"}, xo{"xo"}, yo{"yo"};

std::optional<Expr> mod_in_pipeline(Func f) {
    struct : Internal::IRMutator {
        using IRMutator::visit;
        Expr mod_op;
        Expr visit(const Internal::Mod *op) override {
            mod_op = op;
            return IRMutator::visit(op);
        }
    } m;
    f.add_custom_lowering_pass(&m, nullptr);
    f.compile_jit();
    return m.mod_op.defined() ? std::make_optional(m.mod_op) : std::nullopt;
}
}  // namespace

TEST(FuseTest, ComplexSchedule) {
    Func f, g;

    Expr e = x * 3 + y;
    f(x, y) = e;
    g(x, y) = e;

    f.compute_root();

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

    Buffer<int> f_buf{32, 32};
    f_buf.set_min(-16, -16);

    Buffer<int> g_buf{32, 32};
    g_buf.set_min(-16, -16);

    f.realize(f_buf);
    g.realize(g_buf);

    for (int yy = -16; yy < 16; yy++) {
        for (int xx = -16; xx < 16; xx++) {
            int expected = xx * 3 + yy;
            EXPECT_EQ(f_buf(xx, yy), expected) << "x = " << xx << ", y = " << yy;
            EXPECT_EQ(g_buf(xx, yy), expected) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(FuseTest, FuseAndVectorize) {
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
        .vectorize(xy, 16, TailStrategy::RoundUp);

    EXPECT_THAT(mod_in_pipeline(f), testing::Eq(std::nullopt));
}

// Test two cases where the fuse arithmetic should vanish due to nested vectorization
TEST(FuseTest, NestedVectorizationSumOfSlices) {
    // The first case should turn into a sum of slices of a vector
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

    EXPECT_THAT(mod_in_pipeline(f), testing::Eq(std::nullopt));
}

TEST(FuseTest, NestedVectorizationReduce) {
    // The second case should turn into a vector reduce instruction, with no modulo in the indexing
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

    EXPECT_THAT(mod_in_pipeline(f), testing::Eq(std::nullopt));
}
