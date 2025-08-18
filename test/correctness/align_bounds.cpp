#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CheckForSelects : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Select *op) override {
        result = true;
    }

public:
    bool result = false;
};

int trace_min, trace_extent;
int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == 2) {
        trace_min = e->coordinates[0];
        trace_extent = e->coordinates[1];
    }
    return 0;
}

}  // namespace

TEST(AlignBoundsTest, ForceBoundsEvenRemoveSelect) {
    // Force the bounds of an intermediate pipeline stage to be even to remove a select
    Func f, g, h;
    Var x;

    f(x) = 3;

    g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

    Param<int> p;
    h(x) = g(x - p) + g(x + p);

    f.compute_root();
    g.compute_root().align_bounds(x, 2).unroll(x, 2).trace_realizations();

    // The lowered IR should contain no selects.
    Module m = g.compile_to_module({p});
    CheckForSelects checker;
    m.functions()[0].body.accept(&checker);
    EXPECT_FALSE(checker.result) << "Lowered code contained a select";

    p.set(3);
    h.jit_handlers().custom_trace = my_trace;
    Buffer<int> result = h.realize({10});

    for (int i = 0; i < 10; i++) {
        int correct = (i & 1) == 1 ? 6 : 22;
        EXPECT_EQ(result(i), correct) << "result(" << i << ") = " << result(i) << " instead of " << correct;
    }

    // Bounds of f should be [-p, 10+2*p] rounded outwards
    EXPECT_EQ(trace_min, -4) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 18) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // Increasing p by one should have no effect
    p.set(4);
    ASSERT_TRUE(result.data());
    h.realize(result);
    EXPECT_EQ(trace_min, -4) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 18) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // But increasing it again should cause a jump of two in the bounds computed.
    ASSERT_TRUE(result.data());
    p.set(5);
    h.realize(result);
    EXPECT_EQ(trace_min, -6) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 22) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
}

TEST(AlignBoundsTest, MisalignWithOffsetForceOddBounds) {
    // Now try a case where we misalign with an offset (i.e. force the
    // bounds to be odd). This should also remove the select.
    Func f, g, h;
    Var x;

    f(x) = 3;

    g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

    Param<int> p;
    h(x) = g(x - p) + g(x + p);

    f.compute_root();
    g.compute_root().align_bounds(x, 2, 1).unroll(x, 2).trace_realizations();

    // The lowered IR should contain no selects.
    Module m = g.compile_to_module({p});
    CheckForSelects checker;
    m.functions()[0].body.accept(&checker);
    EXPECT_FALSE(checker.result) << "Lowered code contained a select";

    p.set(3);
    h.jit_handlers().custom_trace = my_trace;
    Buffer<int> result = h.realize({10});

    for (int i = 0; i < 10; i++) {
        int correct = (i & 1) == 1 ? 6 : 22;
        EXPECT_EQ(result(i), correct) << "result(" << i << ") = " << result(i) << " instead of " << correct;
    }

    // Now the min/max should stick to odd numbers
    EXPECT_EQ(trace_min, -3) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 16) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // Increasing p by one should have no effect
    p.set(4);
    h.realize(result);
    EXPECT_EQ(trace_min, -5) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 20) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // But increasing it again should cause a jump of two in the bounds computed.
    p.set(5);
    h.realize(result);
    EXPECT_EQ(trace_min, -5) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 20) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
}

TEST(AlignBoundsTest, AlignExtentButNotMin) {
    // Now try a case where we align the extent but not the min.
    Func f, g, h;
    Var x;

    f(x) = 3;

    g(x) = select(x % 2 == 0, f(x + 1), f(x - 1) + 8);

    Param<int> p;
    h(x) = g(x - p) + g(x + p);

    f.compute_root();
    g.compute_root().align_extent(x, 32).trace_realizations();

    p.set(3);
    h.jit_handlers().custom_trace = my_trace;
    Buffer<int> result = h.realize({10});

    for (int i = 0; i < 10; i++) {
        int correct = (i & 1) == 1 ? 6 : 22;
        EXPECT_EQ(result(i), correct) << "result(" << i << ") = " << result(i) << " instead of " << correct;
    }

    // Now the min/max should stick to odd numbers
    EXPECT_EQ(trace_min, -3) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 32) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // Increasing p by one should have no effect
    p.set(4);
    h.realize(result);
    EXPECT_EQ(trace_min, -4) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 32) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";

    // But increasing it again should cause a jump of two in the bounds computed.
    p.set(5);
    h.realize(result);
    EXPECT_EQ(trace_min, -5) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
    EXPECT_EQ(trace_extent, 32) << "Wrong bounds: [" << trace_min << ", " << trace_extent << "]";
}

TEST(AlignBoundsTest, StridedLoadsWithAlignment) {
    // Try a case where aligning a buffer means that strided loads can
    // do dense aligned loads and then shuffle. This used to trigger a
    // bug in codegen.
    Func f, g;
    Var x;

    f(x) = x;

    // Do strided loads of every possible alignment
    Expr e = 0;
    for (int i = -32; i <= 32; i++) {
        e += f(3 * x + i);
    }
    g(x) = e;

    f.compute_root();
    g.bound(x, 0, 1024).vectorize(x, 16, TailStrategy::RoundUp);

    // Just check if it crashes
    EXPECT_NO_THROW(g.realize({1024}));
}
