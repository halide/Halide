#include "Halide.h"
#include <gtest/gtest.h>

#include "check_call_graphs.h"

#include <map>

namespace {
using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

enum class TestMode {
    CallGraphs,
    Correctness,
};

class RFactorCallGraphsTest : public ::testing::TestWithParam<TestMode> {
};
}  // namespace

TEST_P(RFactorCallGraphsTest, Simple) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = 40;
    RDom r(10, 20, 30, 40);
    g(r.x, r.y) = max(g(r.x, r.y) + f(r.x, r.y), g(r.x, r.y));
    g.reorder_storage(y, x);

    Var u("u");
    Func intm = g.update(0).rfactor(r.y, u);
    intm.compute_root();
    intm.vectorize(u, 8);
    intm.update(0).vectorize(r.x, 2);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::max(40 + x + y, 40) : 40;
        };
        ASSERT_TRUE(check_image(im, func) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, ReorderSplit) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    RDom r(10, 20, 20, 30);

    f(x, y) = x - y;
    f.compute_root();

    g(x, y) = 1;
    g(r.x, r.y) += f(r.x, r.y);
    g.update(0).reorder({r.y, r.x});

    RVar rxi("rxi"), rxo("rxo");
    g.update(0).split(r.x, rxo, rxi, 2);

    Var u("u"), v("v");
    Func intm1 = g.update(0).rfactor({{rxo, u}, {r.y, v}});
    Func intm2 = g.update(0).rfactor(r.y, v);
    intm2.compute_root();
    intm1.compute_at(intm2, rxo);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm2.name(), g.name()}},
            {intm2.name(), {intm1.name(), intm2.name()}},
            {intm1.name(), {f.name(), intm1.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        ASSERT_TRUE(check_image(im, func) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, MultiSplit) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    RDom r(10, 20, 20, 30);

    f(x, y) = x - y;
    f.compute_root();

    g(x, y) = 1;
    g(r.x, r.y) += f(r.x, r.y);
    g.update(0).reorder({r.y, r.x});

    RVar rxi("rxi"), rxo("rxo"), ryi("ryi"), ryo("ryo"), ryoo("ryoo"), ryoi("ryoi");
    Var u("u"), v("v"), w("w");

    g.update(0).split(r.x, rxo, rxi, 2);
    Func intm1 = g.update(0).rfactor({{rxo, u}, {r.y, v}});

    g.update(0).split(r.y, ryo, ryi, 2, TailStrategy::GuardWithIf);
    g.update(0).split(ryo, ryoo, ryoi, 4, TailStrategy::GuardWithIf);
    Func intm2 = g.update(0).rfactor({{rxo, u}, {ryoo, v}, {ryoi, w}});
    intm2.compute_root();
    intm1.compute_root();

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm2.name(), g.name()}},
            {intm2.name(), {intm1.name(), intm2.name()}},
            {intm1.name(), {f.name(), intm1.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        ASSERT_TRUE(check_image(im, func) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, ReorderFuseWrapper) {
    Func f("f"), g("g");
    Var x("x"), y("y"), z("z");

    RDom r(5, 10, 5, 10, 5, 10);

    f(x, y, z) = x + y + z;
    g(x, y, z) = 1;
    g(r.x, r.y, r.z) += f(r.x, r.y, r.z);
    g.update(0).reorder({r.y, r.x});

    RVar rf("rf");
    g.update(0).fuse(r.x, r.y, rf);
    g.update(0).reorder({r.z, rf});

    Var u("u"), v("v");
    Func intm = g.update(0).rfactor(r.z, u);
    RVar rfi("rfi"), rfo("rfo");
    intm.update(0).split(rf, rfi, rfo, 2);
    intm.compute_at(g, r.z);

    Func wrapper = f.in(intm).compute_root();
    f.compute_root();

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {wrapper.name(), {f.name()}},
            {intm.name(), {wrapper.name(), intm.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int> im = g.realize({20, 20, 20});
        auto func = [](int x, int y, int z) {
            return ((5 <= x && x <= 14) && (5 <= y && y <= 14) &&
                    (5 <= z && z <= 14)) ?
                       x + y + z + 1 :
                       1;
        };
        ASSERT_TRUE(check_image(im, func) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, NonTrivialLHS) {
    Func a("a"), b("b"), c("c");
    Var x("x"), y("y"), z("z");

    RDom r(5, 10, 5, 10, 5, 10);

    a(x, y, z) = x;
    b(x, y, z) = x + y;
    c(x, y, z) = x + y + z;

    a.compute_root();
    b.compute_root();
    c.compute_root();

    Buffer<int> im_ref(20, 20, 20);

    {
        Func f("f"), g("g");
        f(x, y) = 1;
        Expr x_clamped = clamp(a(r.x, r.y, r.z), 0, 19);
        Expr y_clamped = clamp(b(r.x, r.y, r.z), 0, 29);
        f(x_clamped, y_clamped) += c(r.x, r.y, r.z);
        f.compute_root();

        g(x, y, z) = 2 * f(x, y);
        im_ref = g.realize({20, 20, 20});
    }

    {
        Func f("f"), g("g");
        f(x, y) = 1;
        Expr x_clamped = clamp(a(r.x, r.y, r.z), 0, 19);
        Expr y_clamped = clamp(b(r.x, r.y, r.z), 0, 29);
        f(x_clamped, y_clamped) += c(r.x, r.y, r.z);
        f.compute_root();

        g(x, y, z) = 2 * f(x, y);

        Var u("u"), v("v");
        RVar rzi("rzi"), rzo("rzo");
        Func intm = f.update(0).rfactor({{r.x, u}, {r.y, v}});
        intm.update(0).split(r.z, rzo, rzi, 2);
        intm.compute_root();

        if (GetParam() == TestMode::CallGraphs) {
            // Check the call graphs.
            CallGraphs expected = {
                {g.name(), {f.name()}},
                {f.name(), {f.name(), intm.name()}},
                {intm.name(), {a.name(), b.name(), c.name(), intm.name()}},
                {a.name(), {}},
                {b.name(), {}},
                {c.name(), {}},
            };
            ASSERT_TRUE(check_call_graphs(g, expected) == 0);
        } else {
            Buffer<int> im = g.realize({20, 20, 20});
            auto func = [im_ref](int x, int y, int z) {
                return im_ref(x, y, z);
            };
            ASSERT_TRUE(check_image(im, func) == 0);
        }
    }
}

TEST_P(RFactorCallGraphsTest, SimpleWithSpecialize) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = 40;
    RDom r(10, 20, 30, 40);
    g(r.x, r.y) = min(f(r.x, r.y) + 2, g(r.x, r.y));

    Param<int> p;
    Var u("u");
    Func intm = g.update(0).specialize(p >= 10).rfactor(r.y, u);
    intm.compute_root();
    intm.vectorize(u, 8);
    intm.update(0).vectorize(r.x, 2);

    if (GetParam() == TestMode::CallGraphs) {
        p.set(20);
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {f.name(), intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        {
            p.set(0);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            ASSERT_TRUE(check_image(im, func) == 0);
        }
        {
            p.set(20);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            ASSERT_TRUE(check_image(im, func) == 0);
        }
    }
}

TEST_P(RFactorCallGraphsTest, RDomWithPredicate) {
    Func f("f"), g("g");
    Var x("x"), y("y"), z("z");

    f(x, y, z) = x + y + z;
    f.compute_root();

    g(x, y, z) = 1;
    RDom r(5, 10, 5, 10, 0, 20);
    r.where(r.x < r.y);
    r.where(r.x + 2 * r.y <= r.z);
    g(r.x, r.y, r.z) += f(r.x, r.y, r.z);

    Var u("u"), v("v");
    Func intm = g.update(0).rfactor({{r.y, u}, {r.x, v}});
    intm.compute_root();
    Var ui("ui"), vi("vi"), t("t");
    intm.tile(u, v, ui, vi, 2, 2).fuse(u, v, t).parallel(t);
    intm.update(0).vectorize(r.z, 2);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int> im = g.realize({20, 20, 20});
        auto func = [](int x, int y, int z) {
            return (5 <= x && x <= 14) && (5 <= y && y <= 14) &&
                           (0 <= z && z <= 19) && (x < y) && (x + 2 * y <= z) ?
                       x + y + z + 1 :
                       1;
        };
        ASSERT_TRUE(check_image(im, func) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, Histogram) {
    int W = 128, H = 128;

    // Compute a random image and its true histogram
    int reference_hist[256];
    for (int i = 0; i < 256; i++) {
        reference_hist[i] = 0;
    }

    Buffer<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = float(rand() & 0x000000ff);
            reference_hist[uint8_t(in(x, y))] += 1;
        }
    }

    Func hist("hist"), g("g");
    Var x("x");

    RDom r(in);
    hist(x) = 0;
    hist(clamp(cast<int>(in(r.x, r.y)), 0, 255)) += 1;
    hist.compute_root();

    Var u("u");
    Func intm = hist.update(0).rfactor(r.y, u);
    intm.compute_root();
    intm.update(0).parallel(u);

    g(x) = hist(x + 10);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {hist.name()}},
            {hist.name(), {intm.name(), hist.name()}},
            {intm.name(), {in.name(), intm.name()}},

        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Buffer<int32_t> histogram = g.realize({10});  // buckets 10-20 only
        for (int i = 10; i < 20; i++) {
            EXPECT_EQ(histogram(i - 10), reference_hist[i]) << "i = " << i;
        }
    }
}

TEST_P(RFactorCallGraphsTest, ParallelDotProduct) {
    int size = 1024;

    Func f("f"), g("g"), a("a"), b("b");
    Var x("x");

    a(x) = x;
    b(x) = x + 2;
    a.compute_root();
    b.compute_root();

    RDom r(0, size);

    Func dot_ref("dot");
    dot_ref() = 0;
    dot_ref() += a(r.x) * b(r.x);
    Buffer<int32_t> ref = dot_ref.realize();

    Func dot("dot");
    dot() = 0;
    dot() += a(r.x) * b(r.x);
    RVar rxo("rxo"), rxi("rxi");
    dot.update(0).split(r.x, rxo, rxi, 128);

    Var u("u");
    Func intm1 = dot.update(0).rfactor(rxo, u);
    RVar rxio("rxio"), rxii("rxii");
    intm1.update(0).split(rxi, rxio, rxii, 8);

    Var v("v");
    Func intm2 = intm1.update(0).rfactor(rxii, v);
    intm2.compute_at(intm1, u);
    intm2.update(0).vectorize(v, 8);

    intm1.compute_root();
    intm1.update(0).parallel(u);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {dot.name(), {intm1.name(), dot.name()}},
            {intm1.name(), {intm2.name(), intm1.name()}},
            {intm2.name(), {a.name(), b.name(), intm2.name()}},
            {a.name(), {}},
            {b.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(dot, expected) == 0);
    } else {
        Buffer<int32_t> im = dot.realize();
        ASSERT_EQ(im(0), ref(0));
    }
}

TEST_P(RFactorCallGraphsTest, Tuple) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = Tuple(x + y, x - y);
    f.compute_root();

    RDom r(10, 20, 30, 40);

    Func ref("ref");
    ref(x, y) = Tuple(1, 3);
    ref(x, y) = Tuple(ref(x, y)[0] + f(r.x, r.y)[0] + 3, min(ref(x, y)[1], f(r.x, r.y)[1]));
    Realization ref_rn = ref.realize({80, 80});

    g(x, y) = Tuple(1, 3);
    g(x, y) = Tuple(g(x, y)[0] + f(r.x, r.y)[0] + 3, min(g(x, y)[1], f(r.x, r.y)[1]));
    g.reorder({y, x});

    Var xi("xi"), yi("yi");
    g.update(0).tile(x, y, xi, yi, 4, 4);

    Var u("u");
    Func intm1 = g.update(0).rfactor(r.y, u);
    RVar rxi("rxi"), rxo("rxo");
    intm1.tile(x, y, xi, yi, 4, 4);
    intm1.update(0).split(r.x, rxo, rxi, 2);

    Var v("v");
    Func intm2 = intm1.update(0).rfactor(rxo, v);
    intm2.compute_at(intm1, rxo);

    intm1.update(0).parallel(u, 2);
    intm1.compute_root();

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", intm1.name() + ".1", g.name() + ".0", g.name() + ".1"}},
            {intm1.name(), {intm2.name() + ".0", intm2.name() + ".1", intm1.name() + ".0", intm1.name() + ".1"}},
            {intm2.name(), {f.name() + ".0", f.name() + ".1", intm2.name() + ".0", intm2.name() + ".1"}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Realization rn = g.realize({80, 80});
        Buffer<int> im1(rn[0]);
        Buffer<int> im2(rn[1]);

        Buffer<int> ref_im1(ref_rn[0]);
        Buffer<int> ref_im2(ref_rn[1]);

        auto func1 = [&ref_im1](int x, int y, int z) {
            return ref_im1(x, y);
        };
        EXPECT_TRUE(check_image(im1, func1) == 0);

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        EXPECT_TRUE(check_image(im2, func2) == 0);
    }
}

TEST_P(RFactorCallGraphsTest, TupleSpecializeRDomPredicate) {
    Func f("f"), g("g");
    Var x("x"), y("y"), z("z");

    f(x, y, z) = Tuple(x + y + z, x - y + z);
    f.compute_root();

    RDom r(5, 20, 5, 20, 5, 20);
    r.where(r.x * r.x + r.z * r.z <= 200);
    r.where(r.y * r.z + r.z * r.z > 100);

    Func ref("ref");
    ref(x, y) = Tuple(1, 3);
    ref(x, y) = Tuple(ref(x, y)[0] * f(r.x, r.y, r.z)[0], ref(x, y)[1] + 2 * f(r.x, r.y, r.z)[1]);
    Realization ref_rn = ref.realize({10, 10});

    g(x, y) = Tuple(1, 3);

    g(x, y) = Tuple(g(x, y)[0] * f(r.x, r.y, r.z)[0], g(x, y)[1] + 2 * f(r.x, r.y, r.z)[1]);

    Param<int> p;
    Param<bool> q;

    Var u("u"), v("v"), w("w");
    Func intm1 = g.update(0).specialize(p >= 5).rfactor({{r.y, v}, {r.z, w}});
    intm1.update(0).parallel(v, 4);
    intm1.compute_root();

    RVar rxi("rxi"), rxo("rxo");
    intm1.update(0).split(r.x, rxo, rxi, 2);
    Var t("t");
    Func intm2 = intm1.update(0).specialize(q).rfactor(rxi, t).compute_root();
    Func intm3 = intm1.update(0).specialize(!q).rfactor(rxo, t).compute_root();
    Func intm4 = g.update(0).rfactor({{r.x, u}, {r.z, w}}).compute_root();
    intm4.update(0).vectorize(u);

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", intm1.name() + ".1", intm4.name() + ".0", intm4.name() + ".1", g.name() + ".0", g.name() + ".1"}},
            {intm1.name(), {intm2.name() + ".0", intm2.name() + ".1", intm3.name() + ".0", intm3.name() + ".1", intm1.name() + ".0", intm1.name() + ".1"}},
            {intm2.name(), {f.name() + ".0", f.name() + ".1", intm2.name() + ".0", intm2.name() + ".1"}},
            {intm3.name(), {f.name() + ".0", f.name() + ".1", intm3.name() + ".0", intm3.name() + ".1"}},
            {intm4.name(), {f.name() + ".0", f.name() + ".1", intm4.name() + ".0", intm4.name() + ".1"}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        {
            p.set(10);
            q.set(true);
            Realization rn = g.realize({10, 10});
            Buffer<int> im1(rn[0]);
            Buffer<int> im2(rn[1]);

            Buffer<int> ref_im1(ref_rn[0]);
            Buffer<int> ref_im2(ref_rn[1]);

            auto func1 = [&ref_im1](int x, int y, int z) {
                return ref_im1(x, y, z);
            };
            EXPECT_TRUE(check_image(im1, func1) == 0);
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            EXPECT_TRUE(check_image(im2, func2) == 0);
        }
        {
            p.set(10);
            q.set(false);
            Realization rn = g.realize({10, 10});
            Buffer<int> im1(rn[0]);
            Buffer<int> im2(rn[1]);

            Buffer<int> ref_im1(ref_rn[0]);
            Buffer<int> ref_im2(ref_rn[1]);

            auto func1 = [&ref_im1](int x, int y, int z) {
                return ref_im1(x, y, z);
            };
            EXPECT_TRUE(check_image(im1, func1) == 0);

            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            EXPECT_TRUE(check_image(im2, func2) == 0);
        }
        {
            p.set(0);
            q.set(true);
            Realization rn = g.realize({10, 10});
            Buffer<int> im1(rn[0]);
            Buffer<int> im2(rn[1]);

            Buffer<int> ref_im1(ref_rn[0]);
            Buffer<int> ref_im2(ref_rn[1]);

            auto func1 = [&ref_im1](int x, int y, int z) {
                return ref_im1(x, y, z);
            };
            EXPECT_TRUE(check_image(im1, func1) == 0);

            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            EXPECT_TRUE(check_image(im2, func2) == 0);
        }
        {
            p.set(0);
            q.set(false);
            Realization rn = g.realize({10, 10});
            Buffer<int> im1(rn[0]);
            Buffer<int> im2(rn[1]);

            Buffer<int> ref_im1(ref_rn[0]);
            Buffer<int> ref_im2(ref_rn[1]);

            auto func1 = [&ref_im1](int x, int y, int z) {
                return ref_im1(x, y, z);
            };
            EXPECT_TRUE(check_image(im1, func1) == 0);

            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            EXPECT_TRUE(check_image(im2, func2) == 0);
        }
    }
}

TEST(RFactorTest, ComplexMultiply) {
    Func f("f"), g("g"), ref("ref");
    Var x("x"), y("y");

    f(x, y) = Tuple(x + y, x - y);
    f.compute_root();

    Param<int> inner_extent, outer_extent;
    RDom r(10, inner_extent, 30, outer_extent);
    inner_extent.set(20);
    outer_extent.set(40);

    ref(x, y) = Tuple(10, 20);
    ref(x, y) = Tuple(ref(x, y)[0] * f(r.x, r.y)[0] - ref(x, y)[1] * f(r.x, r.y)[1],
                      ref(x, y)[0] * f(r.x, r.y)[1] + ref(x, y)[1] * f(r.x, r.y)[0]);

    g(x, y) = Tuple(10, 20);
    g(x, y) = Tuple(g(x, y)[0] * f(r.x, r.y)[0] - g(x, y)[1] * f(r.x, r.y)[1],
                    g(x, y)[0] * f(r.x, r.y)[1] + g(x, y)[1] * f(r.x, r.y)[0]);

    RVar rxi("rxi"), rxo("rxo");
    g.update(0).split(r.x, rxo, rxi, 2);

    Var u("u");
    Func intm = g.update(0).rfactor(rxo, u);
    intm.compute_root();
    intm.update(0).vectorize(u, 2);

    Realization ref_rn = ref.realize({80, 80});
    Buffer<int> ref_im1(ref_rn[0]);
    Buffer<int> ref_im2(ref_rn[1]);
    Realization rn = g.realize({80, 80});
    Buffer<int> im1(rn[0]);
    Buffer<int> im2(rn[1]);

    auto func1 = [&ref_im1](int x, int y, int z) {
        return ref_im1(x, y);
    };
    EXPECT_TRUE(check_image(im1, func1) == 0);

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    EXPECT_TRUE(check_image(im2, func2) == 0);
}

TEST(RFactorTest, ArgMin) {
    Func f("f"), g("g"), ref("ref");
    Var x("x"), y("y"), z("z");

    f(x, y) = x + y;
    f.compute_root();

    Param<int> inner_extent, outer_extent;
    RDom r(10, inner_extent, 30, outer_extent);
    inner_extent.set(20);
    outer_extent.set(40);

    ref() = Tuple(10, 20.0f, 30.0f);
    ref() = Tuple(min(ref()[0], f(r.x, r.y)),
                  select(ref()[0] < f(r.x, r.y), ref()[1], cast<float>(r.x)),
                  select(ref()[0] < f(r.x, r.y), ref()[2], cast<float>(r.y)));

    g() = Tuple(10, 20.0f, 30.0f);
    g() = Tuple(min(g()[0], f(r.x, r.y)),
                select(g()[0] < f(r.x, r.y), g()[1], cast<float>(r.x)),
                select(g()[0] < f(r.x, r.y), g()[2], cast<float>(r.y)));

    RVar rxi("rxi"), rxo("rxo");
    g.update(0).split(r.x, rxo, rxi, 2);

    Var u("u");
    Func intm = g.update(0).rfactor(rxo, u);
    intm.compute_root();
    intm.update(0).vectorize(u, 2);

    Realization ref_rn = ref.realize();
    Buffer<int> ref_im1(ref_rn[0]);
    Buffer<float> ref_im2(ref_rn[1]);
    Buffer<float> ref_im3(ref_rn[2]);
    Realization rn = g.realize();
    Buffer<int> im1(rn[0]);
    Buffer<float> im2(rn[1]);
    Buffer<float> im3(rn[2]);

    auto func1 = [&ref_im1](int x, int y, int z) {
        return ref_im1(x, y);
    };
    EXPECT_TRUE(check_image(im1, func1) == 0);

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    EXPECT_TRUE(check_image(im2, func2) == 0);

    auto func3 = [&ref_im3](int x, int y, int z) {
        return ref_im3(x, y);
    };
    EXPECT_TRUE(check_image(im3, func3) == 0);
}

TEST(RFactorTest, CheckAllocationBound) {
    Var x("x"), u("u");
    Func f("f"), g("g");

    RDom r(0, 31);
    f(x) = x;
    g(x) = 1;
    g(r.x) += f(r.x);

    RVar rxo("rxo"), rxi("rxi");
    g.update(0).split(r.x, rxo, rxi, 2);
    f.compute_at(g, rxo);
    g.update(0).rfactor({{rxo, u}}).compute_at(g, rxo);

    f.trace_realizations();
    g.jit_handlers().custom_trace = [](JITUserContext *user_context, const halide_trace_event_t *e) {
        // The schedule implies that f will be stored from 0 to 1
        if (e->event == halide_trace_begin_realization && std::string(e->func) == "f") {
            EXPECT_LE(e->coordinates[1], 2)
                << "Realized " << e->func << " on [" << e->coordinates[0] << ", " << e->coordinates[1] << "]";
        }
        return 0;
    };
    g.realize({23});
}

TEST(RFactorTest, TileReorder) {
    Func ref("ref"), f("f");
    Var x("x");
    RDom r(0, 8, 0, 8);

    // Create an input with random values
    Buffer<uint8_t> input(8, 8, "input");
    for (int yy = 0; yy < 8; ++yy) {
        for (int xx = 0; xx < 8; ++xx) {
            input(xx, yy) = (rand() % 256);
        }
    }

    ref(x) = 0;
    ref(input(r.x, r.y) % 8) += 1;

    f(x) = 0;
    f(input(r.x, r.y) % 8) += 1;

    Var u("u"), v("v"), ui("ui"), vi("vi");
    f.update()
        .rfactor({{r.x, u}, {r.y, v}})
        .compute_root()
        .update()
        .tile(u, v, ui, vi, 4, 4)
        .parallel(u)
        .parallel(v);

    Buffer<int> im_ref = ref.realize({8});
    Buffer<int> im = f.realize({8});
    auto func = [&im_ref](int x, int y) {
        return im_ref(x, y);
    };
    ASSERT_TRUE(check_image(im, func) == 0);
}

TEST_P(RFactorCallGraphsTest, TuplePartialReduction) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = Tuple(x + y, x - y);
    f.compute_root();

    RDom r(10, 20, 30, 40);

    Func ref("ref");
    ref(x, y) = Tuple(1, 3);
    ref(x, y) = Tuple(ref(x, y)[0] + f(r.x, r.y)[0] + 3, ref(x, y)[1]);
    Realization ref_rn = ref.realize({80, 80});

    g(x, y) = Tuple(1, 3);
    g(x, y) = Tuple(g(x, y)[0] + f(r.x, r.y)[0] + 3, g(x, y)[1]);
    g.reorder({y, x});

    Var xi("xi"), yi("yi");
    g.update(0).tile(x, y, xi, yi, 4, 4);

    Var u("u");
    Func intm1 = g.update(0).rfactor(r.y, u);
    RVar rxi("rxi"), rxo("rxo");
    intm1.tile(x, y, xi, yi, 4, 4);
    intm1.update(0).split(r.x, rxo, rxi, 2);

    Var v("v");
    Func intm2 = intm1.update(0).rfactor(rxo, v);
    intm2.compute_at(intm1, rxo);

    intm1.update(0).parallel(u, 2);
    intm1.compute_root();

    if (GetParam() == TestMode::CallGraphs) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", g.name() + ".0"}},
            {intm1.name(), {intm2.name() + ".0", intm1.name() + ".0"}},
            {intm2.name(), {f.name() + ".0", intm2.name() + ".0"}},
            {f.name(), {}},
        };
        ASSERT_TRUE(check_call_graphs(g, expected) == 0);
    } else {
        Realization rn = g.realize({80, 80});
        Buffer<int> im1(rn[0]);
        Buffer<int> im2(rn[1]);

        Buffer<int> ref_im1(ref_rn[0]);
        Buffer<int> ref_im2(ref_rn[1]);

        auto func1 = [&ref_im1](int x, int y, int z) {
            return ref_im1(x, y);
        };
        EXPECT_TRUE(check_image(im1, func1) == 0);

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        EXPECT_TRUE(check_image(im2, func2) == 0);
    }
}

TEST(RFactorTest, SelfAssignment) {
    Func g("g");
    Var x("x"), y("y");

    g(x, y) = x + y;
    RDom r(0, 10, 0, 10);
    g(r.x, r.y) = g(r.x, r.y);

    Var u("u");
    Func intm = g.update(0).rfactor(r.y, u);
    intm.compute_root();

    Buffer<int> im = g.realize({10, 10});
    auto func = [](int x, int y, int z) {
        return x + y;
    };
    ASSERT_TRUE(check_image(im, func) == 0);
}

TEST(RFactorTest, InlinedRFactorWithDisappearingRVar) {
    ImageParam in1(Float(32), 1);

    Var x("x"), r("r"), u("u");
    RVar ro("ro"), ri("ri");
    Func f("f"), g("g");
    Func sum1("sum1");

    RDom rdom(0, 16);
    g(r, x) = in1(x);
    f(x) = sum(rdom, g(rdom, x), sum1);

    {
        // Some of the autoschedulers execute code like the below, which can
        // erase an RDom from the LHS and RHS of a Func, but not from the dims
        // list, which confused the implementation of rfactor (see
        // https://github.com/halide/Halide/issues/8282)
        using namespace Halide::Internal;
        std::vector<Function> outputs = {f.function()};
        auto env = build_environment(outputs);

        for (auto &iter : env) {
            iter.second.lock_loop_levels();
        }

        inline_function(sum1.function(), g.function());
    }

    sum1.compute_root()
        .update(0)
        .split(rdom, ro, ri, 8, TailStrategy::GuardWithIf)
        .rfactor({{ro, u}})
        .compute_root();

    // This would crash with a missing symbol error prior to #8282 being fixed.
    ASSERT_NO_THROW(f.compile_jit());
}

TEST(RFactorTest, PreciseBounds) {
    // From issue: https://github.com/halide/Halide/issues/8600
    Var x("x"), y("y");
    RDom r(0, 10, 0, 10);

    // Create an input with random values
    Buffer<uint8_t> input(10, 10, "input");
    for (int yy = 0; yy < 10; ++yy) {
        for (int xx = 0; xx < 10; ++xx) {
            input(xx, yy) = (rand() % 256);
        }
    }

    Func f;

    f() = 0;
    f() += input(r.x, r.y);
    RVar rxo, rxi, ryo, ryi;
    Func intm = f.update()
                    .tile(r.x, r.y, rxo, ryo, rxi, ryi, 4, 4)
                    .rfactor({{rxi, x}, {ryi, y}});

    intm.compute_root();

    ASSERT_NO_THROW(f.realize());
}

namespace {
enum class MaxRFactorTestVariant {
    BitwiseOr,
    LogicalOr,
};

class MaxRFactorTest : public ::testing::TestWithParam<MaxRFactorTestVariant> {
};
}  // namespace

TEST_P(MaxRFactorTest, Check) {
    RDom r(0, 16);
    RVar ri("ri");
    Var x("x"), y("y"), u("u");

    ImageParam in(Float(32), 2);

    const auto make_reduce = [&](const char *name) {
        Func reduce(name);
        reduce(y) = Float(32).min();
        switch (GetParam()) {
        case MaxRFactorTestVariant::BitwiseOr:
            reduce(y) = select(reduce(y) > cast(reduce.type(), in(r, y)) | is_nan(reduce(y)), reduce(y), cast(reduce.type(), in(r, y)));
            break;
        case MaxRFactorTestVariant::LogicalOr:
            reduce(y) = select(reduce(y) > cast(reduce.type(), in(r, y)) || is_nan(reduce(y)), reduce(y), cast(reduce.type(), in(r, y)));
            break;
        }
        return reduce;
    };

    Func reference = make_reduce("reference");

    Func reduce = make_reduce("reduce");
    reduce.update(0).split(r, r, ri, 8).rfactor(ri, u);

    float tests[][16] = {
        {NAN, 0.29f, 0.19f, 0.68f, 0.44f, 0.40f, 0.39f, 0.53f, 0.23f, 0.21f, 0.85f, 0.19f, 0.37f, 0.03f, 0.14f, 0.64f},
        {0.98f, 0.65f, 0.86f, 0.16f, 0.14f, 0.91f, 0.74f, 0.99f, 0.91f, 0.01f, 0.11f, 0.59f, 0.05f, 0.90f, 0.93f, NAN},
        {0.84f, 0.14f, 0.99f, 0.19f, 0.63f, 0.12f, 0.51f, 0.67f, NAN, 0.34f, 0.89f, 0.93f, 0.72f, 0.69f, 0.58f, 0.63f},
        {0.44f, 0.12f, 0.00f, 0.30f, 0.80f, 0.88f, 0.95f, 0.12f, 0.90f, 0.99f, 0.67f, 0.71f, 0.35f, 0.67f, 0.18f, 0.93f},
    };

    Buffer<float, 2> buf{tests};
    in.set(buf);

    Buffer<float, 1> ref_vals = reference.realize({4}, get_jit_target_from_environment().with_feature(Target::StrictFloat));
    Buffer<float, 1> fac_vals = reduce.realize({4}, get_jit_target_from_environment().with_feature(Target::StrictFloat));

    for (int i = 0; i < 4; i++) {
        if (std::isnan(fac_vals(i)) && std::isnan(ref_vals(i))) {
            continue;
        }
        EXPECT_EQ(fac_vals(i), ref_vals(i)) << "i = " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(RFactorTest, RFactorCallGraphsTest,
                         ::testing::Values(TestMode::CallGraphs, TestMode::Correctness));
INSTANTIATE_TEST_SUITE_P(RFactorTest, MaxRFactorTest,
                         ::testing::Values(MaxRFactorTestVariant::BitwiseOr, MaxRFactorTestVariant::LogicalOr));
