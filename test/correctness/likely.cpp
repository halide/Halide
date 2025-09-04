#include "Halide.h"
#include <gtest/gtest.h>

#include <utility>

using namespace Halide;

// The performance of this behavior is tested in
// test/performance/boundary_conditions.cpp

namespace {
using namespace Halide::Internal;
using std::string;

// Count the number of stores to a given func, and the number of calls to sin
struct Counter final : IRVisitor {
    string func;
    int store_count = 0, sin_count = 0;
    explicit Counter(string f)
        : func(std::move(f)) {
    }

    using IRVisitor::visit;

    void visit(const Store *op) override {
        IRVisitor::visit(op);
        if (op->name == func) {
            store_count++;
        }
    }

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == "sin_f32") {
            sin_count++;
        }
    }
};

// Check that the number of calls to sin is correct.
struct CheckSinCount final : IRMutator {
    Counter c{""};
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        s.accept(&c);
        return s;
    }
};

// Check that the number of stores to a given func is correct
struct CheckStoreCount final : IRMutator {
    Counter c;
    explicit CheckStoreCount(string f)
        : c(std::move(f)) {
    }

    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        s.accept(&c);
        return s;
    }
};

void count_partitions(Func g, int correct) {
    CheckStoreCount checker(g.name());
    g.add_custom_lowering_pass(&checker, nullptr);
    g.compile_to_module(g.infer_arguments());
    EXPECT_EQ(checker.c.store_count, correct) << "in Func " << g.name();
}

void count_sin_calls(Func g, int correct) {
    CheckSinCount checker;
    g.add_custom_lowering_pass(&checker, nullptr);
    g.compile_to_module(g.infer_arguments());
    EXPECT_EQ(checker.c.sin_count, correct) << "in Func " << g.name();
}
}  // namespace

class LikelyTest : public ::testing::Test {
protected:
    Func f{"f"};
    Var x{"x"};

    void SetUp() override {
        f(x) = x;
        f.compute_root();
    }
};

// Halide will partition a loop into three pieces in a few
// situations. The pieces are 1) a messy prologue, 2) a clean
// steady state, and 3) a messy epilogue. One way to trigger this
// is if you use a boundary condition helper:
TEST_F(LikelyTest, BoundaryConditionPartitioning) {
    Func g = BoundaryConditions::repeat_edge(f, {{0, 100}});
    count_partitions(g, 3);

    // check that disabling works.
    g.partition(x, Partition::Never);
    count_partitions(g, 1);
}

// If you vectorize or otherwise split, then the last vector
// (which gets shifted leftwards) is its own partition. This
// removes some clamping logic from the inner loop.
TEST_F(LikelyTest, VectorizationPartitioning) {
    Func g;
    g(x) = f(x);
    g.vectorize(x, 8);
    count_partitions(g, 2);

    // check that disabling works.
    g.partition(x, Partition::Never);
    count_partitions(g, 1);
}

// The slicing applies to every loop level starting from the outermost one,
// but only recursively simplifies the clean steady state. It either splits
// things three (start, middle, end). So adding a boundary condition to a 2D
// computation will produce 5 code paths for the top, bottom, left, right,
// and center of the image. With explicit control over loop partitioning, we
// might produce more or fewer.
TEST_F(LikelyTest, TwoDimensionalBoundaryConditions) {
    Var y;
    Func g;
    g(x, y) = x + y;
    g.compute_root();
    Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
    count_partitions(h, 5);
}

TEST_F(LikelyTest, ExplicitPartitionControlNeverYAlwaysX) {
    Var y;
    Func g;
    g(x, y) = x + y;
    g.compute_root();
    Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
    h.partition(x, Partition::Always);
    h.partition(y, Partition::Never);
    count_partitions(h, 3);  // We expect left-center-right
}

TEST_F(LikelyTest, ExplicitPartitionControlNeverXAlwaysY) {
    Var y;
    Func g;
    g(x, y) = x + y;
    g.compute_root();
    Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
    h.partition(x, Partition::Never);
    h.partition(y, Partition::Always);
    count_partitions(h, 3);  // We expect top-middle-bottom
}

TEST_F(LikelyTest, ExplicitPartitionControlNeverXAndY) {
    Var y;
    Func g;
    g(x, y) = x + y;
    g.compute_root();
    Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
    h.partition(x, Partition::Never);
    h.partition(y, Partition::Never);
    count_partitions(h, 1);
}

TEST_F(LikelyTest, ExplicitPartitionControlAlwaysXAndY) {
    Var y;
    Func g;
    g(x, y) = x + y;
    g.compute_root();
    Func h = BoundaryConditions::mirror_image(g, {{0, 10}, {0, 10}});
    h.partition(x, Partition::Always);
    h.partition(y, Partition::Always);
    // All loops get partitioned, including the tails of outer loops, so we expect 9 zones:
    /*
       ----------------------------------------------
       | top left    | top middle    | top right    |
       | ------------------------------------------ |
       | left        | middle        | right        |
       | ------------------------------------------ |
       | bottom left | bottom middle | bottom right |
       ----------------------------------------------
    */
    count_partitions(h, 9);
}

// If you split and also have a boundary condition, or have
// multiple boundary conditions at play (e.g. because you're
// blurring an inlined Func that uses a boundary condition), then
// there are still only three partitions. The steady state is the
// slice of the loop where *all* of the boundary conditions and
// splitting logic simplify away.
TEST_F(LikelyTest, MultipleBoundaryConditions) {
    Func g = BoundaryConditions::mirror_interior(f, {{0, 10}});
    Func h;
    h(x) = g(x - 1) + g(x + 1);
    h.vectorize(x, 8);
    count_partitions(h, 3);
}

// You can manually control the splitting behavior using the
// 'likely' intrinsic. When used on one side of a select, min,
// max, or clamp, it tags the select, min, max, or clamp as likely
// to simplify to that expression in the steady state case, and
// tries to solve for loop variable values for which this is true.
TEST_F(LikelyTest, LikelyIntrinsicSimpleCondition) {
    // So this code should produce a prologue that evaluates to sin(x), and
    // a steady state that evaluates to 1:
    Func g;
    g(x) = select(x < 10, sin(x), likely(1.0f));
    // There should be two partitions
    count_partitions(g, 2);
    // But only one should call sin
    count_sin_calls(g, 1);
}

TEST_F(LikelyTest, LikelyIntrinsicComplexCondition) {
    // This code should produce a prologue and epilogue that
    // evaluate sin(x), and a steady state that evaluates to 1:
    Func g;
    g(x) = select(x < 10 || x > 100, sin(x), likely(1.0f));
    // There should be three partitions
    count_partitions(g, 3);
    // With calls to sin in the prologue and epilogue.
    count_sin_calls(g, 2);
}

// As a specialize case, we treat clamped ramps as likely to
// simplify to the clamped expression. This handles the many
// existing cases where people have written their boundary
// condition manually using clamp.
TEST_F(LikelyTest, ClampedRamps) {
    Func g;
    g(x) = f(clamp(x, 0, 10));  // treated as clamp(likely(x), 0, 10)
    g.vectorize(x, 8);
    count_partitions(g, 3);

    // check that disabling works.
    g.partition(x, Partition::Never);
    count_partitions(g, 1);
}

// Using the likely intrinsic pulls some IR relating to the
// condition outside of the loop. We'd better check that this
// respects lets and doesn't do any combinatorial expansion. We'll
// do this with a nasty comparison:
TEST_F(LikelyTest, ComplexComparisonsWithLets) {
    Func g;
    Var y;

    // Have an inner reduction loop that the comparisons depend on
    // to make things harder.
    RDom r(0, 5);

    const int N = 25;

    // Make some nasty expressions to compare to.
    Expr e[N];
    e[0] = y;
    for (int i = 1; i < N; i++) {
        e[i] = e[i - 1] * e[i - 1] + y + r;
    }
    // Make a nasty condition that uses all of these.
    Expr nasty = cast<bool>(1);
    for (int i = 0; i < N; i++) {
        nasty = nasty && (x * (i + 1) < e[i]);
    }

    // Have an innermost loop over c to complicate things further.
    Var c;
    g(c, x, y) = sum(select(nasty, likely(10), c + r));

    // Check that it doesn't take the age of the world to compile,
    // and that it produces the right number of partitions.
    count_partitions(g, 3);
}

// Make sure partitions that occur outside of the actual bounds
// don't mess things up.
TEST_F(LikelyTest, PartitionsBeyondActualBounds) {
    Func g;
    Param<int> limit;
    g(x) = select(x > limit, likely(3), 2);

    // If either of these realize calls iterates from 0 to limit,
    // and then from limit to 10, we'll have a nice segfault.
    limit.set(10000000);
    Buffer<int> result = g.realize({10});

    limit.set(-10000000);
    result = g.realize({10});
}

// Test for the bug described in https://github.com/halide/Halide/issues/7929
TEST_F(LikelyTest, BoundaryConditionsWithParameter) {
    Func f_local, g, h;
    Var x, y;

    f_local(x, y) = x;
    f_local.compute_root();

    Param<int> p;
    g = BoundaryConditions::repeat_edge(f_local, {{0, p}, {Expr(), Expr()}});

    h(x, y) = g(x, y) + g(x, y + 1) + g(x, y + 2);

    count_partitions(h, 3);

    // Same thing with vectorization too.
    h.vectorize(x, 8);
    count_partitions(h, 3);
}
