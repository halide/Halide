#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CountStores : public IRVisitor {
public:
    int count;

    CountStores()
        : count(0) {
    }

protected:
    using IRVisitor::visit;

    void visit(const Store *op) override {
        count++;
    }
};

class CheckStoreCount : public IRMutator {
public:
    int expected_stores;
    CheckStoreCount(int expected) : expected_stores(expected) {}
    Stmt mutate(const Stmt &s) override {
        CountStores c;
        s.accept(&c);
        EXPECT_EQ(c.count, expected_stores) << "There were " << c.count << " stores. There were supposed to be " << expected_stores;
        return s;
    }
};

}  // namespace

TEST(TupleUndef, UndefStoresRemoved) {
    Buffer<int> a(1024, 1024), b(1024, 1024);
    const int A = (int)0xdeadbeef;
    const int B = (int)0xf00dcafe;

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x + y, undef<int32_t>());
    f(x, y) = Tuple(f(x, y)[0] + undef<int32_t>(), f(x, y)[1] + 2);

    // There should be two stores: the undef stores should have been removed.
    f.add_custom_lowering_pass(new CheckStoreCount(2));

    // Pre-fill with unlikely values so we can verify that undef bits are untouched.
    a.fill(A);
    b.fill(B);
    f.realize({a, b});
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x + y;
            int correct_b = B + 2;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleUndef, PartialUndefRemoval) {
    Buffer<int> a(1024, 1024), b(1024, 1024);
    const int A = (int)0xdeadbeef;
    const int B = (int)0xf00dcafe;

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(x, y);
    f(x, y) = Tuple(undef<int>(), select(x < 20, 20 * f(x, y)[0], undef<int>()));

    // There should be three stores: the undef store to the 1st element of
    // the Tuple in the update definition should have been removed.
    f.add_custom_lowering_pass(new CheckStoreCount(3));

    a.fill(A);
    b.fill(B);
    f.realize({a, b});
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = x;
            int correct_b = (x < 20) ? 20 * x : y;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleUndef, UndefInClampedArgs) {
    Buffer<int> a(1024, 1024), b(1024, 1024);
    const int A = (int)0xdeadbeef;
    const int B = (int)0xf00dcafe;

    Var x("x"), y("y");
    Func f("f"), g("g");

    f(x, y) = {0, 0};

    RDom r(0, 10);
    Expr arg_0 = clamp(select(r.x < 2, 13, undef<int>()), 0, 100);
    Expr arg_1 = clamp(select(r.x < 2, 23, undef<int>()), 0, 100);
    f(arg_0, arg_1) = {f(arg_0, arg_1)[0] + 10, f(arg_0, arg_1)[1] + 5};

    a.fill(A);
    b.fill(B);
    f.realize({a, b});
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x == 13) && (y == 23) ? 20 : 0;
            int correct_b = (x == 13) && (y == 23) ? 10 : 0;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleUndef, AllUndefTuple) {
    Buffer<int> a(1024, 1024), b(1024, 1024);
    const int A = (int)0xdeadbeef;
    const int B = (int)0xf00dcafe;

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = Tuple(undef<int32_t>(), undef<int32_t>());

    // There should be no stores since all Tuple values are undef.
    f.add_custom_lowering_pass(new CheckStoreCount(0));

    // Pre-fill with unlikely values so we can verify that undef bits are untouched.
    a.fill(A);
    b.fill(B);
    f.realize({a, b});
}
