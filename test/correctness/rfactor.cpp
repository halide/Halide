#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cstdio>
#include <map>

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
            return -1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::max(40 + x + y, 40) : 40;
        };
        if (check_image(im, func)) {
            return -1;
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
            return -1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        if (check_image(im, func)) {
            return -1;
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
            return -1;
        }
    } else {
        Buffer<int> im = g.realize({80, 80});
        auto func = [](int x, int y, int z) {
            return ((10 <= x && x <= 29) && (20 <= y && y <= 49)) ? x - y + 1 : 1;
        };
        if (check_image(im, func)) {
            return -1;
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
            return -1;
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
            return -1;
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
                return -1;
            }
        } else {
            Buffer<int> im = g.realize({20, 20, 20});
            auto func = [im_ref](int x, int y, int z) {
                return im_ref(x, y, z);
            };
            if (check_image(im, func)) {
                return -1;
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
            return -1;
        }
    } else {
        {
            p.set(0);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            if (check_image(im, func)) {
                return -1;
            }
        }
        {
            p.set(20);
            Buffer<int> im = g.realize({80, 80});
            auto func = [](int x, int y, int z) {
                return (10 <= x && x <= 29) && (30 <= y && y <= 69) ? std::min(x + y + 2, 40) : 40;
            };
            if (check_image(im, func)) {
                return -1;
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
            return -1;
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
            return -1;
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
            return -1;
        }
    } else {
        Buffer<int32_t> histogram = g.realize({10});  // buckets 10-20 only
        for (int i = 10; i < 20; i++) {
            if (histogram(i - 10) != reference_hist[i]) {
                printf("Error: bucket %d is %d instead of %d\n",
                       i, histogram(i), reference_hist[i]);
                return -1;
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
            return -1;
        }
    } else {
        Buffer<int32_t> im = dot.realize();
        if (ref(0) != im(0)) {
            printf("result = %d instead of %d\n", im(0), ref(0));
            return -1;
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
            return -1;
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
            return -1;
        }

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        if (check_image(im2, func2)) {
            return -1;
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
            return -1;
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
                return -1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return -1;
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
                return -1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return -1;
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
                return -1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return -1;
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
                return -1;
            }
            auto func2 = [&ref_im2](int x, int y, int z) {
                return ref_im2(x, y, z);
            };
            if (check_image(im2, func2)) {
                return -1;
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
        return -1;
    }

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    if (check_image(im2, func2)) {
        return -1;
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
        return -1;
    }

    auto func2 = [&ref_im2](int x, int y, int z) {
        return ref_im2(x, y);
    };
    if (check_image(im2, func2)) {
        return -1;
    }

    auto func3 = [&ref_im3](int x, int y, int z) {
        return ref_im3(x, y);
    };
    if (check_image(im3, func3)) {
        return -1;
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
            exit(-1);
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
        return -1;
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
            return -1;
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
            return -1;
        }

        auto func2 = [&ref_im2](int x, int y, int z) {
            return ref_im2(x, y);
        };
        if (check_image(im2, func2)) {
            return -1;
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
        return -1;
    }
    return 0;
}

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
        {"parallel dot product rfactor test: checking call graphs...", parallel_dot_product_rfactor_test<true>},
        {"parallel dot product rfactor test: checking output img correctness...", parallel_dot_product_rfactor_test<false>},
        {"tuple partial reduction rfactor test: checking call graphs...", tuple_partial_reduction_rfactor_test<true>},
        {"tuple partial reduction rfactor test: checking output img correctness...", tuple_partial_reduction_rfactor_test<false>},
        {"check allocation bound test", check_allocation_bound_test},
        {"rfactor tile reorder test: checking output img correctness...", rfactor_tile_reorder_test},
        {"complex multiply rfactor test", complex_multiply_rfactor_test},
        {"argmin rfactor test", argmin_rfactor_test},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
