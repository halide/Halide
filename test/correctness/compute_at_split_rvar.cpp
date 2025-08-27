#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL int count(int x) {
    return call_counter++;
}
HalideExtern_1(int, count, int);

class ComputeAtSplitRVarTest : public ::testing::Test {
protected:
    void SetUp() override {
        call_counter = 0;
    }
};
}  // namespace

TEST_F(ComputeAtSplitRVarTest, SplitInnerVar) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 2);
    f.compute_at(g, ri);

    Buffer<int> im = g.realize({10});
    EXPECT_EQ(call_counter, 10) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        EXPECT_EQ(im(i), i) << "im(" << i << ") = " << im(i) << " instead of " << i;
    }
}

TEST_F(ComputeAtSplitRVarTest, SplitOuterVar) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 2);
    f.compute_at(g, ro).unroll(x);

    Buffer<int> im = g.realize({10});
    EXPECT_EQ(call_counter, 10) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        EXPECT_EQ(im(i), i) << "im(" << i << ") = " << im(i) << " instead of " << i;
    }
}

TEST_F(ComputeAtSplitRVarTest, SplitInnerVarUnrolled) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 2).unroll(ri);
    f.compute_at(g, ri);

    Buffer<int> im = g.realize({10});
    EXPECT_EQ(call_counter, 10) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        EXPECT_EQ(im(i), i) << "im(" << i << ") = " << im(i) << " instead of " << i;
    }
}

TEST_F(ComputeAtSplitRVarTest, SplitReorderedInnerVar) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 2).reorder(ro, ri);
    f.compute_at(g, ro);

    Buffer<int> im = g.realize({10});
    EXPECT_EQ(call_counter, 10) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        int correct = (i / 2) + ((i % 2 == 0) ? 0 : 5);
        EXPECT_EQ(im(i), correct) << "im(" << i << ") = " << im(i) << " instead of " << correct;
    }
}

TEST_F(ComputeAtSplitRVarTest, SplitTwiceFuseOuterVars) {
    Func f, g;
    Var x;
    RDom r(0, 20);
    RVar rio, rii, ri, ro, fused;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 4).split(ri, rio, rii, 2).fuse(rio, ro, fused);
    f.compute_at(g, fused);

    Buffer<int> im = g.realize({20});
    EXPECT_EQ(call_counter, 20) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        int correct = i;
        EXPECT_EQ(im(i), correct) << "im(" << i << ") = " << im(i) << " instead of " << correct;
    }
}

TEST_F(ComputeAtSplitRVarTest, SplitNonFactorGuardWithIf) {
    Func f, g;
    Var x;
    RDom r(0, 10);
    RVar ri, ro;
    f(x) = count(x);
    g(x) = 0;
    g(r) = f(r);

    g.update().split(r, ro, ri, 3);
    f.compute_at(g, ro);

    Buffer<int> im = g.realize({10});
    EXPECT_EQ(call_counter, 10) << "Wrong number of calls to f: " << call_counter;
    for (int i = 0; i < im.width(); i++) {
        EXPECT_EQ(im(i), i) << "im(" << i << ") = " << im(i) << " instead of " << i;
    }
}
