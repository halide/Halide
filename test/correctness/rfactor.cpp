#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cmath>
#include <cstdio>
#include <map>

// MSVC doesn't define these constants
#if !defined(M_PI)
#define M_PI 3.14159265358979323846264338327950288
#endif

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

template<bool compile_module>
int simple_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::max(40 + x + y, 40) : 40;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int rfactor_wrapper_test() {
    // A global wrapper on an rfactor intermediate. The reducing Func's call to
    // the intermediate must follow the wrapper (external edge), but the
    // intermediate's self-reference in its own update must not (following would
    // make intm -> wrapper -> intm a cycle).
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
    Func intm_w = intm.in();
    intm.compute_root();
    intm_w.compute_root();

    if (compile_module) {
        // g calls the wrapper (not intm directly); the wrapper calls intm; and
        // intm still self-references intm rather than the wrapper.
        CallGraphs expected = {
            {g.name(), {intm_w.name(), g.name()}},
            {intm_w.name(), {intm.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::max(40 + x + y, 40) : 40;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int reorder_split_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm2.name(), g.name()}},
            {intm2.name(), {intm1.name(), intm2.name()}},
            {intm1.name(), {f.name(), intm1.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int multi_split_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm2.name(), g.name()}},
            {intm2.name(), {intm1.name(), intm2.name()}},
            {intm1.name(), {f.name(), intm1.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int reorder_fuse_wrapper_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {wrapper.name(), {f.name()}},
            {intm.name(), {wrapper.name(), intm.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({20, 20, 20});
        auto func = [](int x, int y, int z) {
            return ((5 <= x && x <= 14) && (5 <= y && y <= 14) &&
                    (5 <= z && z <= 14)) ?
                       x + y + z + 1 :
                       1;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int non_trivial_lhs_rfactor_test() {
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

        if (compile_module) {
            // Check the call graphs.
            CallGraphs expected = {
                {g.name(), {f.name()}},
                {f.name(), {f.name(), intm.name()}},
                {intm.name(), {a.name(), b.name(), c.name(), intm.name()}},
                {a.name(), {}},
                {b.name(), {}},
                {c.name(), {}},
            };
            if (check_call_graphs(g, expected) != 0) {
                return 1;
            }
        } else {
            Buffer<int> im = g.realize({20, 20, 20});
            auto func = [im_ref](int x, int y, int z) {
                return im_ref(x, y, z);
            };
            if (check_image(im, func)) {
                return 1;
            }
        }
    }
    return 0;
}

template<bool compile_module>
int simple_rfactor_with_specialize_test() {
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

    if (compile_module) {
        p.set(20);
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {f.name(), intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        {
            p.set(0);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            if (check_image(im, func)) {
                return 1;
            }
        }
        {
            p.set(20);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            if (check_image(im, func)) {
                return 1;
            }
        }
    }
    return 0;
}

template<bool compile_module>
int rdom_with_predicate_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm.name(), g.name()}},
            {intm.name(), {f.name(), intm.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int> im = g.realize({20, 20, 20});
        auto func = [](int x, int y, int z) {
            return (5 <= x && x <= 14) && (5 <= y && y <= 14) &&
                           (0 <= z && z <= 19) && (x < y) && (x + 2 * y <= z) ?
                       x + y + z + 1 :
                       1;
        };
        if (check_image(im, func)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int histogram_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {hist.name()}},
            {hist.name(), {intm.name(), hist.name()}},
            {intm.name(), {in.name(), intm.name()}},

        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int32_t> histogram = g.realize({10});  // buckets 10-20 only
        for (int i = 10; i < 20; i++) {
            if (histogram(i - 10) != reference_hist[i]) {
                printf("Error: bucket %d is %d instead of %d\n",
                       i, histogram(i), reference_hist[i]);
                return 1;
            }
        }
    }
    return 0;
}

template<bool compile_module>
int parallel_dot_product_rfactor_test() {
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

    Buffer<int32_t> im = dot.realize();

    if (compile_module) {
        // Check the call graphs.

        CallGraphs expected = {
            {dot.name(), {intm1.name(), dot.name()}},
            {intm1.name(), {intm2.name(), intm1.name()}},
            {intm2.name(), {a.name(), b.name(), intm2.name()}},
            {a.name(), {}},
            {b.name(), {}},
        };
        if (check_call_graphs(dot, expected) != 0) {
            return 1;
        }
    } else {
        Buffer<int32_t> im = dot.realize();
        if (ref(0) != im(0)) {
            printf("result = %d instead of %d\n", im(0), ref(0));
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int tuple_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", intm1.name() + ".1", g.name() + ".0", g.name() + ".1"}},
            {intm1.name(), {intm2.name() + ".0", intm2.name() + ".1", intm1.name() + ".0", intm1.name() + ".1"}},
            {intm2.name(), {f.name() + ".0", f.name() + ".1", intm2.name() + ".0", intm2.name() + ".1"}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Realization rn = g.realize({80, 80});
        Buffer<int> im1(rn[0]);
        Buffer<int> im2(rn[1]);

        Buffer<int> ref_im1(ref_rn[0]);
        Buffer<int> ref_im2(ref_rn[1]);

        auto func1 = [&ref_im1](int x, int y, int z) {
            return ref_im1(x, y);
        };
        if (check_image(im1, func1)) {
            return 1;
        }

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        if (check_image(im2, func2)) {
            return 1;
        }
    }
    return 0;
}

template<bool compile_module>
int tuple_specialize_rdom_predicate_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", intm1.name() + ".1", intm4.name() + ".0", intm4.name() + ".1", g.name() + ".0", g.name() + ".1"}},
            {intm1.name(), {intm2.name() + ".0", intm2.name() + ".1", intm3.name() + ".0", intm3.name() + ".1", intm1.name() + ".0", intm1.name() + ".1"}},
            {intm2.name(), {f.name() + ".0", f.name() + ".1", intm2.name() + ".0", intm2.name() + ".1"}},
            {intm3.name(), {f.name() + ".0", f.name() + ".1", intm3.name() + ".0", intm3.name() + ".1"}},
            {intm4.name(), {f.name() + ".0", f.name() + ".1", intm4.name() + ".0", intm4.name() + ".1"}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
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
            if (check_image(im1, func1)) {
                return 1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return 1;
            }
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
            if (check_image(im1, func1)) {
                return 1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return 1;
            }
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
            if (check_image(im1, func1)) {
                return 1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return 1;
            }
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
            if (check_image(im1, func1)) {
                return 1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return 1;
            }
        }
    }
    return 0;
}

int complex_multiply_rfactor_test() {
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
    if (check_image(im1, func1)) {
        return 1;
    }

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    if (check_image(im2, func2)) {
        return 1;
    }

    return 0;
}

int argmin_rfactor_test() {
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
    if (check_image(im1, func1)) {
        return 1;
    }

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    if (check_image(im2, func2)) {
        return 1;
    }

    auto func3 = [&ref_im3](int x, int y, int z) {
        return ref_im3(x, y);
    };
    if (check_image(im3, func3)) {
        return 1;
    }

    return 0;
}

int saturating_add_rfactor_test() {
    Func f("f"), g("g"), ref("ref");
    Var x("x"), y("y"), z("z");

    f(x) = cast<uint8_t>(x);
    f.compute_root();

    Param<int> inner_extent;
    RDom r(10, inner_extent);
    inner_extent.set(6);
    uint8_t max_int = 255;

    g() = Tuple(cast<uint8_t>(0), cast<uint8_t>(0));
    g() = Tuple(select(g()[0] > max_int - 3 * f(r.x), max_int, g()[0] + 3 * f(r.x)),
                select(g()[1] > max_int - 9 * f(r.x), max_int, 9 * f(r.x) + g()[1]));

    RVar rxi("rxi"), rxo("rxo");
    g.update(0).split(r.x, rxo, rxi, 2);

    Var u("u");
    Func intm = g.update(0).rfactor(rxo, u);
    intm.compute_root();
    intm.update(0).vectorize(u, 2);

    Realization rn = g.realize();
    Buffer<uint8_t> im1(rn[0]);
    Buffer<uint8_t> im2(rn[1]);

    auto func1 = [](int x, int y, int z) {
        int ret = 0;
        for (int i = 10; i < 16; i++) {
            ret += 3 * i;
        }
        return std::min(ret, 255);
    };
    if (check_image(im1, func1)) {
        return 1;
    }

    auto func2 = [](int x, int y, int z) {
        int ret = 0;
        for (int i = 10; i < 16; i++) {
            ret += 9 * i;
        }
        return std::min(ret, 255);
    };
    if (check_image(im2, func2)) {
        return 1;
    }

    return 0;
}

enum class InlineReductionVariant {
    ArgMin,
    ArgMax,
};

template<InlineReductionVariant variant>
int inline_reductions_test() {
    using namespace ConciseCasts;
    constexpr float pi = static_cast<float>(M_PI);

    Func f{"f"};
    Var x("x");
    f(x) = sin(f32(x) / 8 * pi);  // argmax should be f(4) = 1.0, argmin should be f(12) = -10.0
    f.compute_root();

    RDom r(0, 32);

    Func g{"reduction"};
    Func output{"g"};

    if constexpr (variant == InlineReductionVariant::ArgMin) {
        output() = argmin(f(r), g);
    } else {
        output() = argmax(f(r), g);
    }

    RVar ro("rxo"), ri("rxi");
    g.update(0).split(r, ro, ri, 2);

    Var u("u");
    Func intm = g.update(0).rfactor(ro, u);
    intm.compute_root();
    intm.update(0).vectorize(u, 2);

    Realization rn = output.realize();
    Buffer<int> sch_idx(rn[0]);
    Buffer<float> sch_val(rn[1]);

    if constexpr (variant == InlineReductionVariant::ArgMin) {
        if (sch_val() != -1.0f || sch_idx() != 12) {
            fprintf(stderr, "Expected argmin to be f(12) = -1.0, got f(%d) = %f\n", sch_idx(), sch_val());
            return 1;
        }
    } else {
        if (sch_val() != 1.0f || sch_idx() != 4) {
            fprintf(stderr, "Expected argmax to be f(4) = 1.0, got f(%d) = %f\n", sch_idx(), sch_val());
            return 1;
        }
    }

    return 0;
}

enum class ArgMaxVariant {
    Explicit,
    TupleSelect
};

enum class ArgMaxTupleOrder {
    IndexFirst,
    ValueFirst,
};

template<ArgMaxVariant variant, ArgMaxTupleOrder order>
int argmax_rfactor_test() {
    using namespace ConciseCasts;
    constexpr float pi = static_cast<float>(M_PI);

    Func f{"f"};
    Var x("x");
    f(x) = sin(f32(x) / 8 * pi);  // argmax should be f(4) = 1.0
    f.compute_root();

    RDom r(0, 32);

    Func g{"g"};

    int value_tup = order == ArgMaxTupleOrder::ValueFirst ? 0 : 1;
    int index_tup = order == ArgMaxTupleOrder::ValueFirst ? 1 : 0;

    if constexpr (order == ArgMaxTupleOrder::ValueFirst) {
        g() = Tuple(f.type().min(), r.x.min());
    } else {
        g() = Tuple(r.x.min(), f.type().min());
    }

    if constexpr (variant == ArgMaxVariant::Explicit) {
        if constexpr (order == ArgMaxTupleOrder::ValueFirst) {
            g() = Tuple(max(f(r), g()[value_tup]), select(g()[value_tup] < f(r), r, g()[index_tup]));
        } else {
            g() = Tuple(select(g()[value_tup] < f(r), r, g()[index_tup]), max(f(r), g()[value_tup]));
        }
    } else {
        static_assert(variant == ArgMaxVariant::TupleSelect);
        if constexpr (order == ArgMaxTupleOrder::ValueFirst) {
            g() = select(g()[value_tup] < f(r), Tuple(f(r), r), g());
        } else {
            g() = select(g()[value_tup] < f(r), Tuple(r, f(r)), g());
        }
    }

    RVar ro("rxo"), ri("rxi");
    g.update(0).split(r, ro, ri, 2);

    Var u("u");
    Func intm = g.update(0).rfactor(ro, u);
    intm.compute_root();
    intm.update(0).vectorize(u, 2);

    Realization rn = g.realize();
    Buffer<float> sch_val(rn[value_tup]);
    Buffer<int> sch_idx(rn[index_tup]);

    if (sch_val() != 1.0f || sch_idx() != 4) {
        fprintf(stderr, "Expected argmax to be f(4) = 1.0, got f(%d) = %f\n", sch_idx(), sch_val());
        return 1;
    }

    return 0;
}

int allocation_bound_test_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    // The schedule implies that f will be stored from 0 to 1
    if (e->event == 2 && std::string(e->func) == "f") {
        if (e->coordinates[1] != 2) {
            printf("Bounds on realization of f were supposed to be [0, 2]\n"
                   "Instead they are: [%d, %d]\n",
                   e->coordinates[0], e->coordinates[1]);
            exit(1);
        }
    }
    return 0;
}

int check_allocation_bound_test() {
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
    g.jit_handlers().custom_trace = allocation_bound_test_trace;
    g.realize({23});

    return 0;
}

int rfactor_tile_reorder_test() {
    Func ref("ref"), f("f");
    Var x("x");
    RDom r(0, 8, 0, 8);

    // Create an input with random values
    Buffer<uint8_t> input(8, 8, "input");
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            input(x, y) = (rand() % 256);
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
    if (check_image(im, func)) {
        return 1;
    }

    return 0;
}

template<bool compile_module>
int tuple_partial_reduction_rfactor_test() {
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

    if (compile_module) {
        // Check the call graphs.
        CallGraphs expected = {
            {g.name(), {intm1.name() + ".0", g.name() + ".0"}},
            {intm1.name(), {intm2.name() + ".0", intm1.name() + ".0"}},
            {intm2.name(), {f.name() + ".0", intm2.name() + ".0"}},
            {f.name(), {}},
        };
        if (check_call_graphs(g, expected) != 0) {
            return 1;
        }
    } else {
        Realization rn = g.realize({80, 80});
        Buffer<int> im1(rn[0]);
        Buffer<int> im2(rn[1]);

        Buffer<int> ref_im1(ref_rn[0]);
        Buffer<int> ref_im2(ref_rn[1]);

        auto func1 = [&ref_im1](int x, int y, int z) {
            return ref_im1(x, y);
        };
        if (check_image(im1, func1)) {
            return 1;
        }

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        if (check_image(im2, func2)) {
            return 1;
        }
    }
    return 0;
}

int self_assignment_rfactor_test() {
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
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int inlined_rfactor_with_disappearing_rvar_test() {
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
    f.compile_jit();
    return 0;
}

// From issue: https://github.com/halide/Halide/issues/8600
int rfactor_precise_bounds_test() {
    Var x("x"), y("y");
    RDom r(0, 10, 0, 10);

    // Create an input with random values
    Buffer<uint8_t> input(10, 10, "input");
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            input(x, y) = (rand() % 256);
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

    Buffer<int> im = f.realize();

    return 0;
}

enum MaxRFactorTestVariant {
    BitwiseOr,
    LogicalOr,
};

template<MaxRFactorTestVariant variant>
int isnan_max_rfactor_test() {
    RDom r(0, 16);
    RVar ri("ri");
    Var x("x"), y("y"), u("u");

    ImageParam in(Float(32), 2);

    const auto make_reduce = [&](const char *name) {
        Func reduce(name);
        reduce(y) = Float(32).min();
        switch (variant) {
        case BitwiseOr:
            reduce(y) = select(reduce(y) > cast(reduce.type(), in(r, y)) | is_nan(reduce(y)), reduce(y), cast(reduce.type(), in(r, y)));
            break;
        case LogicalOr:
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
        if (fac_vals(i) != ref_vals(i)) {
            std::cerr << "At index " << i << ", expected: " << ref_vals(i) << ", got: " << fac_vals(i) << "\n";
            return 1;
        }
    }

    return 0;
}

// hoist_invariants() lifts a loop-invariant factor out of a sum reduction:
//   sum_k(scale(i,j) * inner(i,j,k)) = scale(i,j) * sum_k(inner(i,j,k))
// It does not change any types: the returned intermediate accumulates the
// factor-free body at its natural type. (Retyping the accumulation for a
// dot-product-friendly integer type is the job of change_type().)
int hoist_invariants_test() {
    ImageParam A{Int(8), 2, "A"};
    ImageParam B{Int(8), 2, "B"};
    ImageParam As{Float(16), 1, "As"};
    ImageParam Bs{Float(16), 1, "Bs"};

    Var i{"i"}, j{"j"};
    RDom k({{0, A.dim(1).extent() / 4 * 4}}, "k");
    RVar ko{"ko"}, ki{"ki"};

    Func C{"C"};
    C(i, j) += widening_mul(As(i), Bs(j)) * cast(Int(32), widening_mul(A(i, k), B(j, k)));
    C.bound(i, 0, A.dim(0).extent());
    C.bound(j, 0, B.dim(0).extent());
    C.update().split(k, ko, ki, 4);

    // widening_mul(As(i), Bs(j)) is invariant in k, so hoisting moves it out of
    // the reduction: the intermediate accumulates only the inner product and the
    // scale is applied once during write-back. The body's type (Float(32)) is
    // unchanged.
    Func C_intm = C.update().hoist_invariants()[0];

    internal_assert(C_intm.types()[0] == Float(32))
        << "hoist_invariants: expected C_intm to keep its natural Float(32) type, got "
        << C_intm.types()[0] << "\n";

    // Numerical correctness: result must match a reference that applies the full
    // non-hoisted reduction.
    const int M = 8, N = 8, K = 16;
    Buffer<int8_t> a_buf(M, K), b_buf(N, K);
    Buffer<float16_t> as_buf(M), bs_buf(N);
    for (int m = 0; m < M; m++) {
        for (int n_k = 0; n_k < K; n_k++) {
            a_buf(m, n_k) = (int8_t)((m + n_k) % 7 - 3);
        }
        as_buf(m) = float16_t((float)(m + 1) * 0.5f);
    }
    for (int n = 0; n < N; n++) {
        for (int n_k = 0; n_k < K; n_k++) {
            b_buf(n, n_k) = (int8_t)((n + n_k + 1) % 5 - 2);
        }
        bs_buf(n) = float16_t((float)(n + 1) * 0.25f);
    }
    A.set(a_buf);
    B.set(b_buf);
    As.set(as_buf);
    Bs.set(bs_buf);

    Buffer<float> result = C.realize({M, N});

    // Reference: plain reduction without hoisting.
    Buffer<float> ref(M, N);
    ref.fill(0.f);
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            for (int kk = 0; kk < K; kk++) {
                ref(m, n) += (float)as_buf(m) * (float)bs_buf(n) *
                             (float)((int32_t)(int16_t)((int16_t)a_buf(m, kk) * (int16_t)b_buf(n, kk)));
            }
        }
    }

    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            internal_assert(std::abs(result(m, n) - ref(m, n)) < 1e-6f)
                << "hoist_invariants mismatch at (" << m << ", " << n << "): "
                << result(m, n) << " vs ref " << ref(m, n) << "\n";
        }
    }

    return 0;
}

// An invariant factor may be nested inside its own sub-product rather than
// sitting as one of a Mul's two immediate operands:
//   (scaleA(i) * castA) * (scaleB(i) * castB)
// -- the way two independently-scaled operands naturally compose. Finding both
// scales requires flattening the whole multiplicative chain into leaves and
// partitioning each one by invariance, not just checking a binary node's two
// immediate children.
int hoist_invariants_scattered_factors_test() {
    const int K = 64;
    ImageParam A{Int(8), 1, "A"};
    ImageParam B{Int(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants()[0];
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<int8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 127;
        b_buf(k) = 127;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 127.0f * 127.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors: got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// Same shape as hoist_invariants_scattered_factors_test, but with UInt(8)
// operands, to exercise the unsigned path.
int hoist_invariants_scattered_factors_unsigned_test() {
    const int K = 64;
    ImageParam A{UInt(8), 1, "A"};
    ImageParam B{UInt(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants()[0];
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<uint8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 255;
        b_buf(k) = 255;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 255.0f * 255.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors (unsigned): got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// hoist_invariants() with outer Min and additive factor:
//   min_k(offset(i) + body(i, k)) = offset(i) + min_k(body(i, k))
// The intermediate accumulates min without the offset; write-back adds it once.
int hoist_invariants_min_test() {
    ImageParam offset_p{Float(32), 1, "offset_p"};
    ImageParam data_p{Float(32), 2, "data_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func C{"C"};
    C(i) = Float(32).max();
    C(i) = min(C(i), offset_p(i) + data_p(i, k));

    Func C_intm = C.update().hoist_invariants()[0];
    C_intm.compute_root();

    const int M = 8;
    Buffer<float> off(M), dat(M, K);
    for (int m = 0; m < M; m++) {
        off(m) = (float)(m + 1);
        for (int kk = 0; kk < K; kk++) {
            dat(m, kk) = (float)(((m * K + kk) % 7) - 3);
        }
    }
    offset_p.set(off);
    data_p.set(dat);

    Buffer<float> result = C.realize({M});

    Buffer<float> ref(M);
    for (int m = 0; m < M; m++) {
        float v = std::numeric_limits<float>::max();
        for (int kk = 0; kk < K; kk++) {
            v = std::min(v, off(m) + dat(m, kk));
        }
        ref(m) = v;
    }

    for (int m = 0; m < M; m++) {
        internal_assert(result(m) == ref(m))
            << "hoist_invariants min mismatch at " << m << ": "
            << result(m) << " vs ref " << ref(m) << "\n";
    }
    return 0;
}

// hoist_invariants() with outer Or (bool) and And factor:
//   or_k(mask(i) && check(i, k)) = mask(i) && or_k(check(i, k))
// The intermediate accumulates or without the mask; write-back applies it once.
int hoist_invariants_or_test() {
    ImageParam mask_p{Bool(), 1, "mask_p"};
    ImageParam check_p{Bool(), 2, "check_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func valid{"valid"};
    valid(i) = cast<bool>(false);
    valid(i) = valid(i) || (mask_p(i) && check_p(i, k));

    Func valid_intm = valid.update().hoist_invariants()[0];
    valid_intm.compute_root();

    const int M = 8;
    Buffer<bool> mask(M), chk(M, K);
    for (int m = 0; m < M; m++) {
        mask(m) = (m % 2 == 0);
        for (int kk = 0; kk < K; kk++) {
            chk(m, kk) = ((m + kk) % 3 == 0);
        }
    }
    mask_p.set(mask);
    check_p.set(chk);

    Buffer<bool> result = valid.realize({M});

    for (int m = 0; m < M; m++) {
        bool ref = false;
        for (int kk = 0; kk < K; kk++) {
            ref = ref || (mask(m) && chk(m, kk));
        }
        internal_assert(result(m) == ref)
            << "hoist_invariants or mismatch at " << m << ": "
            << (int)result(m) << " vs ref " << (int)ref << "\n";
    }
    return 0;
}

// hoist_invariants() must not disturb strict_float: the reduction below rounds
// each term to float before summing, and both the reference and the hoisted
// intermediate must observe that same rounding (giving exactly 0).
int hoist_invariants_strict_float_test() {
    Buffer<int32_t> data(2);
    data(0) = 16777217;
    data(1) = -16777216;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = 0.0f;
    f() += 1.5f * strict_float(cast<float>(data(k)));

    Func intm = f.update().hoist_invariants()[0];
    intm.compute_root();

    internal_assert(intm.types()[0] == Float(32))
        << "hoist_invariants strict_float: expected intm to remain Float(32), got "
        << intm.types()[0] << "\n";

    Buffer<float> result = f.realize();
    internal_assert(result() == 0.0f)
        << "hoist_invariants strict_float mismatch: " << result()
        << " vs ref 0\n";

    return 0;
}

// A loop-invariant RDom predicate must be preserved on the write-back update.
// When enabled is false, the original reduction executes no iterations, so the
// hoisted factor (and its failing require) must not be evaluated.
int hoist_invariants_predicated_rdom_test() {
    Param<bool> enabled{"enabled"};
    RDom r(0, 4, "r");
    r.where(enabled);

    Func f{"f"};
    f() = 0;
    f() += require(enabled, 2, "hoisted factor evaluated outside RDom predicate") * (r + 1);

    Func intm = f.update().hoist_invariants()[0];
    intm.compute_root();

    Module module = f.compile_to_module(f.infer_arguments(), "hoist_invariants_predicated_rdom");
    bool inside_predicate = false;
    bool found_writeback = false;
    for (const LoweredFunc &lowered_func : module.functions()) {
        visit_with(
            lowered_func.body,
            [&](auto *self, const IfThenElse *op) {
                const bool old_inside_predicate = inside_predicate;
                inside_predicate |= expr_uses_var(op->condition, enabled.name());
                (*self)(op->then_case);
                inside_predicate = old_inside_predicate;

                if (op->else_case.defined()) {
                    (*self)(op->else_case);
                }
            },
            [&](auto *self, const Store *op) {
                const bool predicated =
                    inside_predicate || expr_uses_var(op->predicate, enabled.name());
                if (op->name == f.name() && predicated) {
                    visit_with(op->value, [&](auto *, const Load *load) {
                        found_writeback |= load->name == intm.name();
                    });
                }
                self->visit_base(op);
            });
    }
    internal_assert(found_writeback)
        << "hoist_invariants predicated RDom: lowered pipeline had no write-back "
        << "guarded by " << enabled.name() << " that reads " << intm.name() << "\n";

    enabled.set(false);
    Buffer<int> disabled_result = f.realize();
    internal_assert(disabled_result() == 0)
        << "hoist_invariants predicated RDom: disabled result was "
        << disabled_result() << ", expected 0\n";

    enabled.set(true);
    Buffer<int> enabled_result = f.realize();
    internal_assert(enabled_result() == 20)
        << "hoist_invariants predicated RDom: enabled result was "
        << enabled_result() << ", expected 20\n";

    return 0;
}

// hoist_invariants() composes with rfactor(): rfactor first preserves r.y as a
// pure Var u, so scale(u) becomes invariant over the intermediate's remaining
// reduction over r.x and can then be hoisted from that intermediate.
int hoist_invariants_after_rfactor_test() {
    ImageParam scale_p{Float(32), 1, "scale_p"};
    ImageParam data_p{Int(32), 2, "data_p"};

    Var u{"u"};
    const int X = 8, Y = 4;
    RDom r(0, X, 0, Y, "r");

    Func f{"f"};
    f() = 0.0f;
    f() += scale_p(r.y) * data_p(r.x, r.y);

    // Preserve r.y as u; the intermediate now reduces only over r.x, and
    // scale_p(u) is invariant across that reduction.
    Func intm = f.update().rfactor({{r.y, u}});
    Func intm2 = intm.update().hoist_invariants()[0];
    intm.compute_root();
    intm2.compute_root();

    Buffer<float> scale(Y);
    Buffer<int> data(X, Y);
    for (int y = 0; y < Y; y++) {
        scale(y) = (float)(y + 1);
        for (int x = 0; x < X; x++) {
            data(x, y) = (x + 2 * y) % 7 - 3;
        }
    }
    scale_p.set(scale);
    data_p.set(data);

    Buffer<float> result = f.realize();

    float ref = 0.0f;
    for (int y = 0; y < Y; y++) {
        int partial = 0;
        for (int x = 0; x < X; x++) {
            partial += data(x, y);
        }
        ref += scale(y) * (float)partial;
    }

    internal_assert(result() == ref)
        << "hoist_invariants after rfactor mismatch: "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// A direct 2D Gaussian blur can be made separable by preserving r.y with
// rfactor(), then hoisting kernel(r.y) out of the intermediate's remaining
// r.x reduction. The factor-free intermediate performs the horizontal blur;
// its write-back applies the vertical kernel weight, and the original Func
// combines those weighted rows.
int hoist_invariants_separable_gaussian_after_rfactor_test() {
    constexpr int radius = 2;
    constexpr int diameter = 2 * radius + 1;
    constexpr int width = 17;
    constexpr int height = 13;

    Buffer<float> kernel(diameter);
    kernel.set_min(-radius);
    const int binomial_weights[diameter] = {1, 4, 6, 4, 1};
    for (int i = 0; i < diameter; i++) {
        kernel(i - radius) = (float)binomial_weights[i] / 16.0f;
    }

    Buffer<float> input(width + 2 * radius, height + 2 * radius);
    input.set_min(-radius, -radius);
    for (int y = input.dim(1).min(); y <= input.dim(1).max(); y++) {
        for (int x = input.dim(0).min(); x <= input.dim(0).max(); x++) {
            input(x, y) = (float)((7 * x + 13 * y + 101) % 29 - 14);
        }
    }

    Var x{"x"}, y{"y"}, dy{"dy"};

    auto make_pipeline = [&](const std::string &func_name, const std::string &rdom_name) {
        RDom r(-radius, diameter, -radius, diameter, rdom_name);
        Func f{func_name};
        f(x, y) = 0.0f;
        f(x, y) += kernel(r.x) * kernel(r.y) * input(x + r.x, y + r.y);
        return std::make_pair(f, r);
    };

    auto [reference, ref_r] = make_pipeline("gaussian_reference", "ref_r");
    auto [blur, r] = make_pipeline("gaussian_blur", "r");

    Func vertical_partials = blur.update().rfactor(r.y, dy);
    Func horizontal = vertical_partials.update().hoist_invariants()[0];

    vertical_partials.compute_root();
    horizontal.compute_root();

    Buffer<float> expected = reference.realize({width, height});
    Buffer<float> actual = blur.realize({width, height});
    for (int yy = 0; yy < height; yy++) {
        for (int xx = 0; xx < width; xx++) {
            const float error = std::abs(actual(xx, yy) - expected(xx, yy));
            internal_assert(error < 1e-5f)
                << "separable Gaussian after rfactor/hoist_invariants mismatch at ("
                << xx << ", " << yy << "): " << actual(xx, yy)
                << " vs ref " << expected(xx, yy) << "\n";
        }
    }

    return 0;
}

// An increment written as a sum of terms with *different* invariant factors has
// no single factor to hoist. Each term gets its own accumulator instead, all
// advanced by one loop, with the write-back applying each factor once.
int hoist_invariants_terms_test() {
    const int K = 64;
    ImageParam G{Int(8), 1, "G"};
    ImageParam H{Int(8), 1, "H"};
    ImageParam A{Float(32), 1, "A"};
    ImageParam B{Float(32), 1, "B"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += A(i) * cast<float>(G(r)) + B(i) * cast<float>(H(r));

    std::vector<Func> Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.size() == 2)
        << "hoist_invariants terms: expected one accumulator per term, got "
        << Acc_intm.size() << "\n";
    for (Func &f : Acc_intm) {
        f.compute_root();
    }

    Buffer<int8_t> g_buf(K), h_buf(K);
    Buffer<float> a_buf(1), b_buf(1);
    for (int k = 0; k < K; k++) {
        g_buf(k) = 3;
        h_buf(k) = 5;
    }
    a_buf(0) = 2.0f;
    b_buf(0) = 7.0f;
    G.set(g_buf);
    H.set(h_buf);
    A.set(a_buf);
    B.set(b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = (2.0f * 3.0f + 7.0f * 5.0f) * (float)K;
    internal_assert(result(0) == expected)
        << "hoist_invariants terms: got " << result(0) << ", expected " << expected << "\n";

    // Terms that share a factor are not worth separate accumulators, and stay in
    // one: sum_k(s*g_k) + sum_k(s*h_k) is sum_k(g_k + h_k) scaled once.
    Func Shared{"Shared"};
    Shared(i) = 0.0f;
    Shared(i) += A(i) * cast<float>(G(r)) + A(i) * cast<float>(H(r));
    std::vector<Func> Shared_intm = Shared.update().hoist_invariants();
    internal_assert(Shared_intm.size() == 1)
        << "hoist_invariants terms: expected terms sharing a factor to share an "
        << "accumulator, got " << Shared_intm.size() << "\n";
    Shared_intm[0].compute_root();

    Buffer<float> shared = Shared.realize({1});
    const float shared_expected = 2.0f * (3.0f + 5.0f) * (float)K;
    internal_assert(shared(0) == shared_expected)
        << "hoist_invariants shared factor: got " << shared(0) << ", expected "
        << shared_expected << "\n";

    return 0;
}

// distribute() multiplies a product of sums out so that hoist_invariants() can
// see terms that were not written as terms. This is the affine-quantized dot
// product: sum_k (d*q_k + m) * (e*p_k), whose expansion
// d*e*sum_k(q_k*p_k) + m*e*sum_k(p_k) has two integer-bodied accumulators where
// the unexpanded form has one float one.
int hoist_invariants_distribute_test() {
    const int K = 32;
    ImageParam Q{Int(8), 1, "Q"};
    ImageParam P{Int(8), 1, "P"};
    ImageParam D{Float(32), 1, "D"};
    ImageParam M{Float(32), 1, "M"};
    ImageParam E{Float(32), 1, "E"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(Q(r)) * D(i) + M(i)) * (cast<float>(P(r)) * E(i));

    std::vector<Func> Acc_intm = Acc.update().distribute().hoist_invariants();
    internal_assert(Acc_intm.size() == 2)
        << "distribute: expected the multiplied-out increment to yield two "
        << "accumulators, got " << Acc_intm.size() << "\n";

    // Both bodies are integer sums of int8 products, so both retype -- and each
    // accumulator is its own Func, so each may take its own target type.
    Func qp = Acc_intm[0].change_type(Int(32));
    Func p_sum = Acc_intm[1].change_type(Int(16));
    internal_assert(qp.types()[0] == Int(32) && p_sum.types()[0] == Int(16))
        << "distribute: retyping the accumulators separately gave "
        << qp.types()[0] << " and " << p_sum.types()[0] << "\n";
    qp.compute_root();
    p_sum.compute_root();

    Buffer<int8_t> q_buf(K), p_buf(K);
    Buffer<float> d_buf(1), m_buf(1), e_buf(1);
    int64_t sum_qp = 0, sum_p = 0;
    for (int k = 0; k < K; k++) {
        q_buf(k) = (int8_t)(k % 15);
        p_buf(k) = (int8_t)((k * 5) % 127 - 63);
        sum_qp += (int64_t)q_buf(k) * p_buf(k);
        sum_p += p_buf(k);
    }
    d_buf(0) = 0.25f;
    m_buf(0) = -0.5f;
    e_buf(0) = 2.0f;
    Q.set(q_buf);
    P.set(p_buf);
    D.set(d_buf);
    M.set(m_buf);
    E.set(e_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 0.25f * 2.0f * (float)sum_qp + -0.5f * 2.0f * (float)sum_p;
    internal_assert(std::abs(result(0) - expected) < 1e-3f)
        << "distribute: got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

#if HALIDE_WITH_EXCEPTIONS
// distribute() is a schedule decision, so it says so when there is nothing to
// multiply out rather than quietly leaving the reduction alone.
int distribute_nothing_to_do_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    ImageParam A{Float(32), 1, "A"};
    Var i{"i"};
    RDom r(0, 8, "r");

    Func f{"f"};
    f(i) = 0.0f;
    f(i) += A(i) * cast<float>(r);

    try {
        f.update().distribute();
    } catch (const Halide::CompileError &e) {
        const std::string msg = e.what();
        internal_assert(msg.find("no product over a sum") != std::string::npos)
            << "distribute() rejected the update for the wrong reason: " << msg << "\n";
        return 0;
    }
    internal_assert(false) << "distribute() accepted an update with nothing to multiply out\n";
    return 0;
}

// The min/max + add hoisting law is only valid for integer types where addition
// has no defined wraparound behavior. For UInt(8), hoisting the invariant 250
// would incorrectly turn min_k((250 + x_k) mod 256) into (250 + min_k(x_k)) mod
// 256, so hoist_invariants() must refuse rather than silently miscompile.
int hoist_invariants_invalid_law_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Buffer<uint8_t> data(2);
    data(0) = 1;
    data(1) = 10;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = UInt(8).max();
    f() = min(f(), cast<uint8_t>(250) + data(k));

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find a distributable loop-invariant "
            "factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error for unsigned min hoisting:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have rejected the unsigned min law!\n");
        return 1;
    }
    return 0;
}

// hoist_invariants() errors when there is no distributable invariant factor to
// hoist, rather than silently behaving like a plain rfactor().
int hoist_invariants_nothing_to_hoist_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam data_p{Int(32), 2, "data_p"};
    Var i{"i"};
    RDom k(0, 8, "k");

    Func f{"f"};
    f(i) = 0;
    f(i) += data_p(i, k);

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find a distributable loop-invariant "
            "factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error when no invariant is hoistable:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have errored when there is nothing to hoist!\n");
        return 1;
    }
    return 0;
}
#endif

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"self assignment rfactor test", self_assignment_rfactor_test},
        {"simple rfactor test: checking call graphs...", simple_rfactor_test<true>},
        {"simple rfactor test: checking output img correctness...", simple_rfactor_test<false>},
        {"rfactor wrapper test: checking call graphs...", rfactor_wrapper_test<true>},
        {"rfactor wrapper test: checking output img correctness...", rfactor_wrapper_test<false>},
        {"reorder split rfactor test: checking call graphs...", reorder_split_rfactor_test<true>},
        {"reorder split rfactor test: checking output img correctness...", reorder_split_rfactor_test<false>},
        {"multiple split rfactor test: checking call graphs...", multi_split_rfactor_test<true>},
        {"multiple split rfactor test: checking output img correctness...", multi_split_rfactor_test<false>},
        {"reorder fuse wrapper rfactor test: checking call graphs...", reorder_fuse_wrapper_rfactor_test<true>},
        {"reorder fuse wrapper rfactor test: checking output img correctness...", reorder_fuse_wrapper_rfactor_test<false>},
        {"non trivial lhs rfactor test: checking call graphs...", non_trivial_lhs_rfactor_test<true>},
        {"non trivial lhs rfactor test: checking output img correctness...", non_trivial_lhs_rfactor_test<false>},
        {"simple rfactor with specialization test: checking call graphs...", simple_rfactor_with_specialize_test<true>},
        {"simple rfactor with specialization test: checking output img correctness...", simple_rfactor_with_specialize_test<false>},
        {"rdom with predicate rfactor test: checking call graphs...", rdom_with_predicate_rfactor_test<true>},
        {"rdom with predicate rfactor test: checking output img correctness...", rdom_with_predicate_rfactor_test<false>},
        {"histogram rfactor test: checking call graphs...", histogram_rfactor_test<true>},
        {"histogram rfactor test: checking output img correctness...", histogram_rfactor_test<false>},
        {"parallel dot product rfactor test: checking call graphs...", parallel_dot_product_rfactor_test<true>},
        {"parallel dot product rfactor test: checking output img correctness...", parallel_dot_product_rfactor_test<false>},
        {"tuple rfactor test: checking call graphs...", tuple_rfactor_test<true>},
        {"tuple rfactor test: checking output img correctness...", tuple_rfactor_test<false>},
        {"tuple specialize rdom predicate rfactor test: checking call graphs...", tuple_specialize_rdom_predicate_rfactor_test<true>},
        {"tuple specialize rdom predicate rfactor test: checking output img correctness...", tuple_specialize_rdom_predicate_rfactor_test<false>},
        {"tuple partial reduction rfactor test: checking call graphs...", tuple_partial_reduction_rfactor_test<true>},
        {"tuple partial reduction rfactor test: checking output img correctness...", tuple_partial_reduction_rfactor_test<false>},
        {"check allocation bound test", check_allocation_bound_test},
        {"rfactor tile reorder test: checking output img correctness...", rfactor_tile_reorder_test},
        {"complex multiply rfactor test", complex_multiply_rfactor_test},
        {"saturating add rfactor test", saturating_add_rfactor_test},
        {"argmin rfactor test", argmin_rfactor_test},
        {"inline reductions test (argmin)", inline_reductions_test<InlineReductionVariant::ArgMin>},
        {"inline reductions test (argmax)", inline_reductions_test<InlineReductionVariant::ArgMax>},
        {"argmax rfactor test (explicit, index first)", argmax_rfactor_test<ArgMaxVariant::Explicit, ArgMaxTupleOrder::IndexFirst>},
        {"argmax rfactor test (tuple, index first)", argmax_rfactor_test<ArgMaxVariant::TupleSelect, ArgMaxTupleOrder::IndexFirst>},
        {"argmax rfactor test (explicit, value first)", argmax_rfactor_test<ArgMaxVariant::Explicit, ArgMaxTupleOrder::ValueFirst>},
        {"argmax rfactor test (tuple, value first)", argmax_rfactor_test<ArgMaxVariant::TupleSelect, ArgMaxTupleOrder::ValueFirst>},
        {"inlined rfactor with disappearing rvar test", inlined_rfactor_with_disappearing_rvar_test},
        {"rfactor bounds tests", rfactor_precise_bounds_test},
        {"isnan max rfactor test (bitwise or)", isnan_max_rfactor_test<BitwiseOr>},
        {"isnan max rfactor test (logical or)", isnan_max_rfactor_test<LogicalOr>},
        {"hoist_invariants test (add/mul)", hoist_invariants_test},
        {"hoist_invariants test (add/mul, scattered factors)", hoist_invariants_scattered_factors_test},
        {"hoist_invariants test (add/mul, scattered factors, unsigned)", hoist_invariants_scattered_factors_unsigned_test},
        {"hoist_invariants test (min/add)", hoist_invariants_min_test},
        {"hoist_invariants test (or/and)", hoist_invariants_or_test},
        {"hoist_invariants test (strict_float preserved)", hoist_invariants_strict_float_test},
        {"hoist_invariants test (predicated RDom)", hoist_invariants_predicated_rdom_test},
        {"hoist_invariants test (after rfactor)", hoist_invariants_after_rfactor_test},
        {"hoist_invariants test (separable Gaussian after rfactor)", hoist_invariants_separable_gaussian_after_rfactor_test},
        {"hoist_invariants test (one accumulator per term)", hoist_invariants_terms_test},
        {"distribute test (affine dot product)", hoist_invariants_distribute_test},
#if HALIDE_WITH_EXCEPTIONS
        {"hoist_invariants test (invalid law rejected)", hoist_invariants_invalid_law_rejected_test},
        {"hoist_invariants test (nothing to hoist rejected)", hoist_invariants_nothing_to_hoist_rejected_test},
        {"distribute test (nothing to distribute rejected)", distribute_nothing_to_do_rejected_test},
#endif
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
