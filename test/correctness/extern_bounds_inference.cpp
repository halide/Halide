#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// An extern stage that translates.
extern "C" HALIDE_EXPORT_SYMBOL int translate(halide_buffer_t *in, int dx, int dy, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].min = out->dim[0].min + dx;
        in->dim[1].min = out->dim[1].min + dy;
        in->dim[0].extent = out->dim[0].extent;
        in->dim[1].extent = out->dim[1].extent;
    } else {
        Runtime::Buffer<uint8_t> out_buf(*out);
        out_buf.translate(dx, dy);
        out_buf.copy_from(Halide::Runtime::Buffer<uint8_t>(*in));
    }
    return 0;
}

void check(const ImageParam &im, const int x, const int w, const int y, const int h) {
    Buffer<uint8_t> buf = im.get();
    ASSERT_NE(buf.data(), nullptr) << "Bounds inference didn't occur!";
    EXPECT_EQ(buf.min(0), x);
    EXPECT_EQ(buf.extent(0), w);
    EXPECT_EQ(buf.min(1), y);
    EXPECT_EQ(buf.extent(1), h);
}

class ExternBoundsInferenceTest : public ::testing::Test {
protected:
    Var x, y;
    static constexpr int W = 30, H = 20;
    ImageParam input{UInt(8), 2};
};
}  // namespace

TEST_F(ExternBoundsInferenceTest, OneExternStage) {
    // Define a pipeline that uses an input image in an extern stage
    // only and do bounds queries.
    Func f;

    std::vector<ExternFuncArgument> args(3);
    args[0] = input;
    args[1] = Expr(3);
    args[2] = Expr(7);

    f.define_extern("translate", args, UInt(8), 2);

    f.infer_input_bounds({W, H});

    // Evaluating the output over [0, 29] x [0, 19] requires the input over [3, 32] x [7, 26]
    check(input, 3, W, 7, H);
}

TEST_F(ExternBoundsInferenceTest, TwoExternStages) {
    // Define a pipeline that uses an input image in two extern stages
    // with different bounds required for each.
    Func f1, f2, g;

    std::vector<ExternFuncArgument> args(3);
    args[0] = input;
    args[1] = Expr(3);
    args[2] = Expr(7);

    f1.define_extern("translate", args, UInt(8), 2);

    args[1] = Expr(8);
    args[2] = Expr(17);
    f2.define_extern("translate", args, UInt(8), 2);

    g(x, y) = f1(x, y) + f2(x, y);

    // Some schedule.
    f1.compute_root();
    f2.compute_at(g, y);
    Var xi, yi;
    g.tile(x, y, xi, yi, 2, 4);

    g.infer_input_bounds({W, H});

    check(input, 3, W + 5, 7, H + 10);
}

TEST_F(ExternBoundsInferenceTest, OneExternOneInternal) {
    // Define a pipeline that uses an input image in an extern stage
    // and an internal stage with different bounds required for each.
    Func f1, f2, g;

    std::vector<ExternFuncArgument> args(3);
    args[0] = input;
    args[1] = Expr(3);
    args[2] = Expr(7);

    f1.define_extern("translate", args, UInt(8), 2);

    f2(x, y) = input(x + 8, y + 17);

    g(x, y) = f1(x, y);
    g(x, y) += f2(x, y);

    f1.compute_at(g, y);
    f2.compute_at(g, x);
    g.reorder(y, x).vectorize(y, 4);
    g.update().unscheduled();

    g.infer_input_bounds({W, H});

    check(input, 3, W + 5, 7, H + 10);
}
