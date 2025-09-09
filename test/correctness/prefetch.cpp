#include "Halide.h"
#include <gtest/gtest.h>

#include <map>
#include <stdio.h>

namespace {

using std::vector;

using namespace Halide;
using namespace Halide::Internal;

class PrefetchTest : public testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
};

template<typename T>
Expr wild() {
    return Variable::make(halide_type_of<T>(), "*");
}

class CollectPrefetches : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::prefetch)) {
            prefetches.push_back(op->args);
        }
    }

public:
    vector<vector<Expr>> prefetches;
};

void check(const vector<vector<Expr>> &expected, vector<vector<Expr>> &result) {
    EXPECT_EQ(result.size(), expected.size()) << "wrong number of prefetches";
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(expected[i].size(), result[i].size()) << "wrong number of prefetch args";
        for (size_t j = 0; j < expected[i].size(); ++j) {
            const Variable *var = expected[i][j].as<Variable>();
            if (bool is_wild = var && var->name == "*"; !is_wild) {
                EXPECT_TRUE(equal(expected[i][j], result[i][j]))
                    << "Expected \"" << expected[i][j] << "\" at arg index "
                    << j << ", got \"" << result[i][j] << "\" instead";
            }
        }
    }
}

Expr get_max_byte_size(const Target &t) {
    // See \ref reduce_prefetch_dimension for max_byte_size
    Expr max_byte_size;
    if (t.has_feature(Target::HVX)) {
        max_byte_size = Expr();
    } else if (t.arch == Target::ARM) {
        max_byte_size = 32;
    } else {
        max_byte_size = 64;
    }
    return max_byte_size;
}

Expr get_stride(const Target &t, const Expr &elem_byte_size) {
    Expr max_byte_size = get_max_byte_size(t);
    return max_byte_size.defined() ? simplify(max_byte_size / elem_byte_size) : 1;
}

}  // namespace

TEST_F(PrefetchTest, BasicPrefetch) {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0);

    f.compute_root();
    g.prefetch(f, x, x, 8);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, SpecializePrefetch) {
    Param<bool> p;

    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0);

    f.compute_root();
    g.specialize(p).prefetch(f, x, x, 8);
    g.specialize_fail("No prefetch");

    Module m = g.compile_to_module({p}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, ComputeAtPrefetch) {
    Func f("f"), g("g"), h("h");
    Var x("x"), xo("xo");

    f(x) = x;
    h(x) = f(x) + 1;
    g(x) = h(0);

    f.compute_root();
    g.split(x, xo, x, 32);
    h.compute_at(g, xo);
    g.prefetch(f, xo, xo, 1);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, NoCallWithinLoopNest) {
    Func f("f"), g("g"), h("h");
    Var x("x");

    f(x) = x;
    h(x) = f(x) + 1;
    g(x) = h(0);

    f.compute_root();
    h.compute_root();
    g.prefetch(f, x, x, 1);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    // There shouldn't be any prefetches since there is no call to 'f'
    // within the loop nest of 'g'
    vector<vector<Expr>> expected = {};
    check(expected, collect.prefetches);
}

// TODO: Warning: Removing prefetch of f$3 at loop nest of g$3.s0.x from location g$3.s0.x + offset 1) since the prefetched area will always be empty.
TEST_F(PrefetchTest, TwoDimensionalBasic) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.prefetch(f, x, y, 8);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, TwoDimensionalSpecialize) {
    Param<bool> p;

    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.specialize(p).prefetch(f, x, y, 8);
    g.specialize_fail("No prefetch");

    Module m = g.compile_to_module({p}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, TwoDimensionalComputeAt) {
    Func f("f"), g("g"), h("h");
    Var x("x"), xo("xo"), y("y");

    f(x, y) = x + y;
    h(x, y) = f(x, y) + 1;
    g(x, y) = h(0, 0);

    f.compute_root();
    g.split(x, xo, x, 32);
    h.compute_at(g, xo);
    g.prefetch(f, xo, y, 1);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(target, 4)}};
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, TwoDimensionalNoCallWithinLoopNest) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x, y) = x + y;
    h(x, y) = f(x, y) + 1;
    g(x, y) = h(0, 0);

    f.compute_root();
    h.compute_root();
    g.prefetch(f, x, y, 1);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    // There shouldn't be any prefetches since there is no call to 'f'
    // within the loop nest of 'g'
    vector<vector<Expr>> expected = {};
    check(expected, collect.prefetches);
}

// TODO: Warning: Removing prefetch of f$7 at loop nest of g$7.s0.x from location g$7.s0.y + offset 1) since the prefetched area will always be empty.
TEST_F(PrefetchTest, TiledVectorizedUnrolled) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .vectorize(x)
        .unroll(y)
        .prefetch(f, x, y, 123, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 4; i++) {
        Expr base = Variable::make(Handle(), f.name());
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        Expr offset = wild<int>();
        Expr extent0 = 1;
        Expr stride0 = get_stride(target, 4);
        if (target.has_feature(Target::HVX)) {
            Expr extent1 = 1;
            Expr stride1 = wild<int>();
            expected.push_back({base, offset, extent0, stride0, extent1, stride1});
        } else {
            expected.push_back({base, offset, extent0, stride0});
        }
    }
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, SplitVectorizedUnrolledReordered) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .split(x, xo, xi, 4)
        .vectorize(xi)
        .unroll(xo)
        .reorder(xi, y, xo)
        .unroll(y)
        // 123/4 because it's supposed to be equivalent to prefetching 123 elements ahead in the x direction.
        // Because this is the xo loop, the correct amount is 123/4.
        .prefetch(f, y, xo, 123 / 4, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 8; i++) {
        Expr base = Variable::make(Handle(), f.name());
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        Expr offset = wild<int>();
        if (target.has_feature(Target::HVX)) {
            Expr extent0 = 4;
            Expr stride0 = get_stride(target, 4);
            Expr extent1 = 1;
            Expr stride1 = wild<int>();
            expected.push_back({base, offset, extent0, stride0, extent1, stride1});
        } else {
            Expr extent0 = 1;
            Expr stride0 = get_stride(target, 4);
            expected.push_back({base, offset, extent0, stride0});
        }
    }
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, SplitRoundUpVectorizedUnrolled) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .split(x, xo, xi, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(xo)
        .reorder(xi, xo, y)
        .unroll(y)
        // 123/4 because it's supposed to be equivalent to prefetching 123 elements ahead in the x direction.
        // Because this is the xo loop, the correct amount is 123/4.
        .prefetch(f, xo, xo, 123 / 4, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 8; i++) {
        Expr base = Variable::make(Handle(), f.name());
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        Expr offset = wild<int>();
        if (target.has_feature(Target::HVX)) {
            Expr extent0 = 4;
            Expr stride0 = get_stride(target, 4);
            Expr extent1 = 1;
            Expr stride1 = wild<int>();
            expected.push_back({base, offset, extent0, stride0, extent1, stride1});
        } else {
            Expr extent0 = 1;
            Expr stride0 = get_stride(target, 4);
            expected.push_back({base, offset, extent0, stride0});
        }
    }
    check(expected, collect.prefetches);
}

TEST_F(PrefetchTest, ComplexScheduleWithHoistPrefetches) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), c("c"), b("b");
    Var ci("ci"), co("co"), xo("xo");

    f(c, x, y, b) = c + x + y + b;

    RDom r(0, 4, 0, 1, 0, 16, "rdom");
    g(c, x, y, b) = 0;
    g(c, x, y, b) += f(r.z, x + r.x, y * 16 + r.y, b);

    h(c, x, y, b) = cast<uint8_t>(g(c, x, y, b));

    f.compute_root();

    g.compute_at(h, co)
        .store_in(MemoryType::Stack)
        .reorder(x, c)
        .vectorize(c, 16, TailStrategy::RoundUp);

    // This schedule is deliberately constructed to unroll a loop with prefetches
    // (so that hoist_prefetches() is tested).
    RVar rco, rci;
    g.update()
        .split(r.z, rco, rci, 16)
        .reorder(rci, c, x, rco, r.x, r.y)
        .vectorize(c, 4, TailStrategy::RoundUp)
        .unroll(c, 4, TailStrategy::RoundUp)
        .atomic()
        .vectorize(rci, 4)
        .unroll(rci)
        .unroll(x)
        .prefetch(f, c, rco, /*offset*/ 1, PrefetchBoundStrategy::NonFaulting);

    h.split(c, co, c, 16, TailStrategy::RoundUp)
        .split(x, xo, x, 4, TailStrategy::RoundUp)
        .reorder(x, c, co, xo, y, b)
        .vectorize(c);

    Module m = h.compile_to_module({}, "", target);
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 4; i++) {
        Expr base = Variable::make(Handle(), f.name());
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        Expr offset = wild<int>();
        if (target.has_feature(Target::HVX)) {
            Expr extent0 = 16;
            Expr stride0 = get_stride(target, 4);
            Expr extent1 = 1;
            Expr stride1 = wild<int>();
            expected.push_back({base, offset, extent0, stride0, extent1, stride1});
        } else {
            Expr extent0 = 1;
            Expr stride0 = get_stride(target, 4);
            expected.push_back({base, offset, extent0, stride0});
        }
    }
    check(expected, collect.prefetches);
}
