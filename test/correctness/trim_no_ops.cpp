#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
class TrimNoOpsTest : public ::testing::Test {};

class CountConditionals : public Internal::IRMutator {
public:
    int count = 0;
    int count_if = 0;
    int count_select = 0;
    bool in_produce = false;

private:
    using Internal::IRMutator::visit;

    Expr visit(const Internal::Select *op) override {
        if (in_produce) {
            count++;
            count_select++;
        }
        return Internal::IRMutator::visit(op);
    }

    Internal::Stmt visit(const Internal::IfThenElse *op) override {
        if (in_produce) {
            count++;
            count_if++;
        }
        return Internal::IRMutator::visit(op);
    }

    Internal::Stmt visit(const Internal::ProducerConsumer *op) override {
        Internal::ScopedValue<bool> v(in_produce, op->is_producer);
        return Internal::IRMutator::visit(op);
    }
};

TEST_F(TrimNoOpsTest, InequalityCondition) {
    // Loop iterations that would be no-ops should be trimmed off.
    Func f;
    Var x;
    f(x) = x;
    f(x) += select(x > 10 && x < 20, 1, 0);
    f(x) += select(x < 10, 0, 1);
    f(x) *= select(x > 20 && x < 30, 2, 1);
    f(x) = select(x >= 60 && x <= 100, 100 - f(x), f(x));

    CountConditionals s;
    f.add_custom_lowering_pass(&s, nullptr);
    Module m = f.compile_to_module({});

    EXPECT_EQ(s.count, 0) << "There were conditionals in the lowered code: \n"
                          << m.functions().front().body;

    // Also check the output is correct
    Buffer<int> im = f.realize({100});
    for (int x = 0; x < im.width(); x++) {
        int correct = x;
        correct += (x > 10 && x < 20) ? 1 : 0;
        correct += (x < 10) ? 0 : 1;
        correct *= (x > 20 && x < 30) ? 2 : 1;
        correct = (x >= 60 && x <= 100) ? (100 - correct) : correct;
        EXPECT_EQ(im(x), correct) << "x = " << x;
    }
}

TEST_F(TrimNoOpsTest, EqualityCondition) {
    // Loop iterations that would be no-ops should be trimmed off. trim_no_ops
    // should be able to handle equality as well.
    Func f;
    Var x, y;
    f(x, y) = x + y;
    f(x, y) += select((x == 10) && (x < y), 1, 0);

    // There should be no selects after trim_no_ops runs
    CountConditionals s;
    f.add_custom_lowering_pass(&s, nullptr);
    Module m = f.compile_to_module({});

    EXPECT_EQ(s.count, 0) << "There were selects in the lowered code: \n"
                          << m.functions().front().body;

    // Also check the output is correct
    Buffer<int> im = f.realize({100, 100});
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            correct += ((x == 10) && (x < y)) ? 1 : 0;
            EXPECT_EQ(im(x, y), correct) << "x = " << x << ", y = " << y;
        }
    }
}

TEST_F(TrimNoOpsTest, TiledHistogram) {
    // Test a tiled histogram
    Func f;
    Var x, y;
    f(x, y) = cast<uint8_t>(random_int());
    f.compute_root();

    Func hist;
    Buffer<int> hist_result;
    {
        RDom r(0, 10, 0, 10, 0, 10, 0, 10);
        Expr xi = r[0] + r[2] * 10, yi = r[1] + r[3] * 10;
        hist(x) = 0;
        hist(f(clamp(xi, 0, 73), clamp(yi, 0, 73))) +=
            select(xi >= 0 && xi <= 73 && yi >= 0 && yi <= 73, 1, 0);

        CountConditionals s;
        hist.add_custom_lowering_pass(&s, nullptr);
        Module m = hist.compile_to_module({});

        EXPECT_EQ(s.count, 0) << "There were selects in the lowered code: \n"
                              << m.functions().front().body;
        hist_result = hist.realize({256});
    }

    // Also check the output is correct.
    Func true_hist;
    {
        RDom r(0, 74, 0, 74);
        true_hist(x) = 0;
        true_hist(f(r.x, r.y)) += 1;
    }
    Buffer<int> true_hist_result = true_hist.realize({256});

    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(hist_result(i), true_hist_result(i)) << "i = " << i;
    }
}

TEST_F(TrimNoOpsTest, TiledIterationOverTriangle) {
    // Test tiled iteration over a triangle, where the condition is an
    // if statement instead of a select.
    Func f;
    Var x, y;
    f(x, y) = select(2 * x < y, 5, undef<int>());

    Var xi, yi;
    f.tile(x, y, xi, yi, 4, 4);

    // Check there are no if statements.
    CountConditionals s;
    f.add_custom_lowering_pass(&s, nullptr);
    Module m = f.compile_to_module({});

    EXPECT_EQ(s.count, 0) << "There were selects or ifs in the lowered code: \n"
                          << m.functions().front().body;
}

TEST_F(TrimNoOpsTest, SelectNotSimplifiedOnGpu) {
    // Test tiled iteration on the gpu if there is support for GPU.
    // The gpu loop variable should not depend on outer gpu loop var.
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f;
    Var x, y;
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    f(r.x, r.y) += select((r.x < r.y) && (r.x == 10), 3, undef<int>());

    RVar rxi, ryi;
    f.update(0).gpu_tile(r.x, r.y, rxi, ryi, 4, 4);

    Buffer<int> im = f.realize({200, 200});

    // There should be no selects after trim_no_ops runs. The select should
    // be lifted out as if condition. We can't trim gpu loop r.x based on the
    // if condition since it depends on gpu outer loop r.y
    Target gpu_target(get_host_target());
    gpu_target.set_feature(Target::CUDA);
    CountConditionals s;
    f.add_custom_lowering_pass(&s, nullptr);
    Module m = f.compile_to_module({}, "", gpu_target);

    EXPECT_EQ(s.count_select, 0) << "There were selects in the lowered code: \n"
                                 << m.functions().front().body;
    EXPECT_EQ(s.count_if, 1) << "There should be 1 if in the lowered code: \n"
                             << m.functions().front().body;

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((x == 10) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 3 : 0;
            }
            EXPECT_EQ(im(x, y), correct) << "x = " << x << ", y = " << y;
        }
    }
}

}  // namespace
