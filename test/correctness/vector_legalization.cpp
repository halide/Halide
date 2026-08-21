#include "Halide.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// This test forces the vector legalization pass (see LegalizeVectors.cpp) to
// run on ordinary Host-scheduled pipelines by setting
// HL_FORCE_VECTOR_LEGALIZATION, something that normally only happens for GPU
// device loops whose backend has a hard vector-lane limit (Vulkan, Metal,
// D3D12Compute). We don't have that kind of GPU coverage in every CI
// environment, so this test exists to catch vector-legalization regressions
// on plain CPU/JIT bots too: everything here is exercised on ordinary
// x86/ARM CPU targets, but with legalization artificially turned on and
// forced down to a variety of (including deliberately awkward, non-power-
// of-two) maximum lane counts.
//
// Each case below defines the same small pipeline twice: once completely
// unscheduled (the reference), and once with an explicit vectorizing
// schedule (the version under test, which is what actually gets legalized).
// The two are realized and compared for exact equality.

using namespace Halide;

namespace {

int num_failures = 0;

void report(const std::string &case_name, int max_lanes, bool ok) {
    if (!ok) {
        printf("FAILED: %s (HL_FORCE_VECTOR_LEGALIZATION=%d)\n", case_name.c_str(), max_lanes);
        num_failures++;
    }
}

template<typename T>
bool compare_1d(const Buffer<T> &a, const Buffer<T> &b) {
    if (a.dim(0).extent() != b.dim(0).extent() || a.dim(0).min() != b.dim(0).min()) {
        printf("  extent/min mismatch\n");
        return false;
    }
    bool ok = true;
    for (int x = a.dim(0).min(); x <= a.dim(0).max(); x++) {
        if (a(x) != b(x)) {
            if (ok) {
                printf("  mismatch at x=%d: %g vs %g\n", x, (double)a(x), (double)b(x));
            }
            ok = false;
        }
    }
    return ok;
}

template<typename T>
bool compare_2d(const Buffer<T> &a, const Buffer<T> &b) {
    if (a.dim(0).extent() != b.dim(0).extent() || a.dim(0).min() != b.dim(0).min() ||
        a.dim(1).extent() != b.dim(1).extent() || a.dim(1).min() != b.dim(1).min()) {
        printf("  extent/min mismatch\n");
        return false;
    }
    bool ok = true;
    for (int y = a.dim(1).min(); y <= a.dim(1).max(); y++) {
        for (int x = a.dim(0).min(); x <= a.dim(0).max(); x++) {
            if (a(x, y) != b(x, y)) {
                if (ok) {
                    printf("  mismatch at (%d, %d): %g vs %g\n", x, y, (double)a(x, y), (double)b(x, y));
                }
                ok = false;
            }
        }
    }
    return ok;
}

// ---------------------------------------------------------------------
// 1. Basic vectorized elementwise store.
// ---------------------------------------------------------------------
bool case_basic_elementwise() {
    const int W = 133;
    Var x("x");

    Func ref("ref");
    ref(x) = x * 3 + 7;
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f("f");
    f(x) = x * 3 + 7;
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 2. Narrow-type (uint8) vectorized store: many lanes per legal vector.
// ---------------------------------------------------------------------
bool case_uint8_narrow_store() {
    const int W = 259;
    Var x("x");

    Func ref("ref");
    ref(x) = cast<uint8_t>((x * 7 + 3) % 251);
    Buffer<uint8_t> ref_buf = ref.realize({W});

    Func f("f");
    f(x) = cast<uint8_t>((x * 7 + 3) % 251);
    f.vectorize(x, 32, TailStrategy::GuardWithIf);
    Buffer<uint8_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 3. Wide-type (int64) vectorized store: few lanes per legal vector.
// ---------------------------------------------------------------------
bool case_int64_wide_store() {
    const int W = 97;
    Var x("x");

    Func ref("ref");
    ref(x) = cast<int64_t>(x) * 1000003;
    Buffer<int64_t> ref_buf = ref.realize({W});

    Func f("f");
    f(x) = cast<int64_t>(x) * 1000003;
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int64_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 4. Deinterleaving load: f(x) = in(2x) + in(2x+1).
// ---------------------------------------------------------------------
bool case_deinterleave_stride2() {
    const int W = 91;
    Var x("x");

    Func in("in");
    in(x) = (x * x) % 997;
    in.compute_root().bound(x, 0, 2 * W);

    Func ref("ref");
    ref(x) = in(2 * x) + in(2 * x + 1);
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f("f");
    f(x) = in(2 * x) + in(2 * x + 1);
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 5. Deinterleaving via nested upsampling (x/2 + 1, applied twice).
// ---------------------------------------------------------------------
bool case_deinterleave_upsample() {
    const int W = 64;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x;
        in.compute_root();

        Func up1("up1");
        up1(x) = in(x / 2 + 1);

        Func up2("up2");
        up2(x) = up1(x / 2 + 1);
        return up2;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.bound(x, 0, W).vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 6. Interleave via select on parity: classic even/odd lane interleave.
// ---------------------------------------------------------------------
bool case_interleave_select_channels() {
    const int W = 128;
    Var x("x");

    Func a("a"), b("a");
    a(x) = x * 2 + 1;
    b(x) = x * 3 + 2;
    a.compute_root();
    b.compute_root();

    Func ref("ref");
    ref(x) = select(x % 2 == 0, a(x / 2), b(x / 2));
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f("f");
    f(x) = select(x % 2 == 0, a(x / 2), b(x / 2));
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 7. Interleaved store: a planar Func written into a channel-interleaved
//    output buffer (dim 0 stride == number of channels). This exercises
//    the exact class of codegen this branch's namesake bug was about.
// ---------------------------------------------------------------------
bool case_interleave_store_stride3() {
    const int W = 16;
    const int C = 3;
    Var x("x"), y("y");

    Func planar("planar");
    planar(x, y) = 3 * x + y;

    Func ref("ref");
    ref(x, y) = planar(x, y);
    Buffer<int32_t> ref_buf = ref.realize({W, C});

    Func planar2("planar2");
    planar2(x, y) = 3 * x + y;

    Func interleaved("interleaved");
    interleaved(x, y) = planar2(x, y);

    Var xy("xy");
    planar2.compute_at(interleaved, xy).vectorize(x, 4, TailStrategy::GuardWithIf);

    interleaved.reorder(y, x)
        .bound(y, 0, C)
        .bound(x, 0, W)
        .fuse(y, x, xy)
        .vectorize(xy, 12, TailStrategy::GuardWithIf);

    interleaved.output_buffer()
        .dim(0)
        .set_stride(C)
        .dim(1)
        .set_min(0)
        .set_stride(1)
        .set_extent(C);

    Buffer<int32_t> out(C, W);
    out.transpose(0, 1);
    interleaved.realize(out);

    return compare_2d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 8. Interleave 4 Funcs via multi-way select on an extra dimension.
// ---------------------------------------------------------------------
bool case_interleave_multiway_select() {
    const int W = 64;
    Var x("x"), y("y");

    auto build = [&](Func &out) {
        Func f1("f1"), f2("f2"), f3("f3"), f4("f4");
        f1(x) = x + 1;
        f2(x) = x * 2;
        f3(x) = x * 3 - 1;
        f4(x) = x * 5 + 2;
        out(x, y) = select(y == 0, f1(x),
                           y == 1, f2(x),
                           y == 2, f3(x),
                           f4(x));
    };

    Func ref("ref");
    build(ref);
    Buffer<int32_t> ref_buf = ref.realize({W, 4});

    Func f("f");
    build(f);
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W, 4});

    return compare_2d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 9. Shuffle via concatenation of two halves (select on range).
// ---------------------------------------------------------------------
bool case_shuffle_concat() {
    const int W = 96;
    Var x("x");

    auto build = [&]() -> Func {
        Func half_a("half_a"), half_b("half_b");
        half_a(x) = x * 2;
        half_b(x) = x * 2 + 1;
        half_a.compute_root();
        half_b.compute_root();

        Func f("f");
        f(x) = select(x < W / 2, half_a(x), half_b(x - W / 2));
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 10. Shuffle via a strided slice (every third lane of a wider vector).
// ---------------------------------------------------------------------
bool case_shuffle_slice_stride3() {
    const int W = 43;
    Var x("x");

    auto build = [&]() -> Func {
        Func big("big");
        big(x) = x * x - 5;
        big.compute_root().bound(x, 0, 3 * W);

        Func f("f");
        f(x) = big(x * 3);
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 4, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 11. Shuffle via full reversal.
// ---------------------------------------------------------------------
bool case_shuffle_reverse() {
    const int W = 87;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x * 5 + 1;
        in.compute_root().bound(x, 0, W);

        Func f("f");
        f(x) = in(W - 1 - x);
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 12. Horizontal VectorReduce: full reduction of a vectorized RVar.
// ---------------------------------------------------------------------
bool case_vector_reduce_horizontal() {
    const int W = 41;
    const int K = 8;
    Var x("x");
    RDom r(0, K);

    auto build = [&](bool vectorize_reduction) -> Func {
        Func in("in");
        in(x) = (x * 7 + 3) % 251;
        in.compute_root().bound(x, 0, K * W);

        Func f("f");
        f(x) = 0;
        f(x) += in(x * K + r);
        if (vectorize_reduction) {
            f.update(0).atomic().vectorize(r);
        }
        return f;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build(true);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 13. Small 2D stencil blur (box sum over a 5x5 window), vectorized
//     store: heavy shuffling to line up shifted stencil taps.
// ---------------------------------------------------------------------
bool case_local_sum_blur_2d() {
    const int W = 24, H = 24;
    Var x("x"), y("y");
    RDom r(-2, 5, -2, 5);

    auto build = [&](bool vec) -> Func {
        Func input("input");
        input(x, y) = 2 * x + 5 * y;

        Func local_sum("local_sum");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);

        Func blurry("blurry");
        blurry(x, y) = local_sum(x, y) / 25;
        if (vec) {
            blurry.vectorize(x, 8, TailStrategy::GuardWithIf);
        }
        return blurry;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W, H});

    Func f = build(true);
    Buffer<int32_t> out = f.realize({W, H});

    return compare_2d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 14. Narrow -> wide cast chain (u8 -> i16 -> i32), vectorized.
// ---------------------------------------------------------------------
bool case_narrow_widen_cast_chain() {
    const int W = 173;
    Var x("x");

    auto build = [&]() -> Func {
        Func in8("in8");
        in8(x) = cast<uint8_t>(x % 256);

        Func in16("in16");
        in16(x) = cast<int16_t>(in8(x)) * 3 - 100;

        Func f("f");
        f(x) = cast<int32_t>(in16(x)) * cast<int32_t>(in16(x));
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 16, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 15. Bit-reinterpretation (int32 <-> float32), vectorized.
// ---------------------------------------------------------------------
bool case_reinterpret_cast() {
    const int W = 61;
    Var x("x");

    auto build = [&]() -> Func {
        Func f("f");
        Expr bits = cast<int32_t>(x * 1000003 + 17);
        Expr as_float = reinterpret<float>(bits);
        Expr doubled = as_float + as_float;
        f(x) = reinterpret<int32_t>(doubled);
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 16. Vectorized store with a GuardWithIf tail on a width that isn't a
//     multiple of the vector width.
// ---------------------------------------------------------------------
bool case_predicated_guardwithif() {
    const int W = 37;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x * x + 1;
        in.compute_root().bound(x, 0, W);

        Func f("f");
        f(x) = in(x) * 3 + 1;
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 17. Vectorized store with an explicit Predicate tail strategy.
// ---------------------------------------------------------------------
bool case_predicated_tailstrategy_predicate() {
    const int W = 45;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x * 3 - 7;
        in.compute_root().bound(x, 0, W);

        Func f("f");
        f(x) = in(x)-x;
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::Predicate);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 18. Boundary-condition-wrapped stencil access (clamp to edge).
// ---------------------------------------------------------------------
bool case_boundary_clamp_stencil() {
    const int W = 40;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x * x % 97;
        in.compute_root().bound(x, 0, W);

        Func clamped = BoundaryConditions::repeat_edge(in, {{0, W}});

        Func f("f");
        f(x) = clamped(x - 3) + clamped(x) + clamped(x + 3);
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 19. Nested split/vectorize/unroll combo across a compute_at pair.
// ---------------------------------------------------------------------
bool case_nested_split_vectorize_unroll() {
    const int W = 32;
    Var x("x"), xo("xo"), xi("xi");

    auto build = [&](bool sched) -> Func {
        Func f("f"), g("g");
        f(x) = 2 * x;
        g(x) = f(x) / 2;
        if (sched) {
            f.compute_at(g, x).split(x, xo, xi, 16).vectorize(xi, 8, TailStrategy::GuardWithIf).unroll(xi);
            g.compute_root().vectorize(x, 16, TailStrategy::GuardWithIf);
        }
        return g;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build(true);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 20. Explicit odd vectorize width (5): remainder handling stress.
// ---------------------------------------------------------------------
bool case_odd_width_vectorize_5() {
    const int W = 53;
    Var x("x");

    auto build = [&]() -> Func {
        Func in("in");
        in(x) = x * x - 3;
        in.compute_root().bound(x, 0, W);

        Func f("f");
        f(x) = in(x)*x - 3;
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 5, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 21. Explicit odd vectorize width (3): remainder handling stress.
// ---------------------------------------------------------------------
bool case_odd_width_vectorize_3() {
    const int W = 34;
    Var x("x");

    auto build = [&]() -> Func {
        Func f("f");
        f(x) = x * 11 - 4;
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 3, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 22. 2D tiled vectorization.
// ---------------------------------------------------------------------
bool case_2d_tile_vectorize() {
    const int W = 24, H = 20;
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    auto build = [&](bool sched) -> Func {
        Func f("f");
        f(x, y) = x * 13 + y * 7;
        if (sched) {
            f.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::GuardWithIf).vectorize(xi, 4, TailStrategy::GuardWithIf);
        }
        return f;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W, H});

    Func f = build(true);
    Buffer<int32_t> out = f.realize({W, H});

    return compare_2d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 23. Gather-like dynamic lookup into a small precomputed table.
// ---------------------------------------------------------------------
bool case_gather_lut() {
    const int W = 96;
    const int LUT_SIZE = 32;
    Var x("x");

    auto build = [&]() -> Func {
        Func lut("lut");
        lut(x) = (x * x + 5) % 251;
        lut.compute_root().bound(x, 0, LUT_SIZE);

        Func idx("idx");
        idx(x) = clamp((x * 37 + 11) % (LUT_SIZE * 2), 0, LUT_SIZE - 1);

        Func f("f");
        f(x) = lut(idx(x));
        return f;
    };

    Func ref = build();
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 24. Tuple-valued (multi-output) vectorized Func.
// ---------------------------------------------------------------------
bool case_tuple_output_vectorize() {
    const int W = 77;
    Var x("x");

    auto build = [&]() -> Func {
        Func f("f");
        f(x) = Tuple(x * 2, x * 3 + 1);
        return f;
    };

    Func ref = build();
    Realization ref_r = ref.realize({W});
    Buffer<int32_t> ref0 = ref_r[0];
    Buffer<int32_t> ref1 = ref_r[1];

    Func f = build();
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    Realization out_r = f.realize({W});
    Buffer<int32_t> out0 = out_r[0];
    Buffer<int32_t> out1 = out_r[1];

    return compare_1d(ref0, out0) & compare_1d(ref1, out1);
}

// ---------------------------------------------------------------------
// 25. Elaborate nested split/fuse/vectorize schedule (adapted from a
//     historical compiler crash, github.com/halide/Halide issue 8038).
//     A vectorized store whose index is a let-bound Variable that
//     simplifies back down to a Ramp is a distinct code path from a
//     literal Ramp, and needs a schedule this convoluted to reach.
// ---------------------------------------------------------------------
bool case_complex_nested_split_fuse_vectorize() {
    const int W = 32, H = 32;
    Var x("x"), y("y");
    RDom r(-2, 5, -2, 5, "rdom_r");

    auto build = [&](bool sched) -> Func {
        Func input("input"), local_sum("local_sum"), blurry("blurry");
        input(x, y) = 2 * x + 5 * y;
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = local_sum(x, y) / 25;

        if (sched) {
            Var yi("yi"), yo("yo"), xi("xi"), xo("xo"),
                yofxi("yofxi"), yofxio("yofxio"), yofxii("yofxii"),
                yofxiifyi("yofxiifyi"), yofxioo("yofxioo"), yofxioi("yofxioi");
            local_sum.split(y, yi, yo, 2, TailStrategy::GuardWithIf)
                .split(x, xi, xo, 5, TailStrategy::Predicate)
                .fuse(yo, xi, yofxi)
                .split(yofxi, yofxio, yofxii, 8, TailStrategy::ShiftInwards)
                .fuse(yofxii, yi, yofxiifyi)
                .split(yofxio, yofxioo, yofxioi, 5, TailStrategy::ShiftInwards)
                .vectorize(yofxiifyi)
                .vectorize(yofxioi);
            local_sum.update(0).unscheduled();
            blurry.split(x, xo, xi, 5, TailStrategy::Auto);
        }
        return blurry;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W, H});

    Func f = build(true);
    Buffer<int32_t> out = f.realize({W, H});

    return compare_2d(ref_buf, out);
}

// ---------------------------------------------------------------------
// 26. Vectorized store/load with tracing enabled. Halide's tracing
//     instrumentation packs a legalized/bundled vector's args through
//     make_struct() calls that get deinterleaved along with everything
//     else, a distinct code path from an untraced pipeline.
// ---------------------------------------------------------------------
int quiet_trace(JITUserContext *, const halide_trace_event_t *) {
    // Just discard trace events; we only care that tracing a legalized
    // pipeline doesn't crash and that the values come out right.
    return 0;
}

bool case_traced_vectorize() {
    const int W = 71;
    Var x("x");

    auto build = [&](bool sched) -> Func {
        Func in("in");
        in(x) = x * 3 + 1;
        in.compute_root().bound(x, 0, W);
        if (sched) {
            in.trace_stores();
        }

        Func f("f");
        f(x) = in(x) * 2 - x;
        if (sched) {
            f.trace_stores();
            f.trace_loads();
        }
        return f;
    };

    Func ref = build(false);
    Buffer<int32_t> ref_buf = ref.realize({W});

    Func f = build(true);
    f.vectorize(x, 8, TailStrategy::GuardWithIf);
    f.jit_handlers().custom_trace = &quiet_trace;
    Buffer<int32_t> out = f.realize({W});

    return compare_1d(ref_buf, out);
}

struct Case {
    const char *name;
    bool (*fn)();
};

// clang-format off
const Case cases[] = {
    {"basic_elementwise",               case_basic_elementwise},
    {"uint8_narrow_store",              case_uint8_narrow_store},
    {"int64_wide_store",                case_int64_wide_store},
    {"deinterleave_stride2",            case_deinterleave_stride2},
    {"deinterleave_upsample",           case_deinterleave_upsample},
    {"interleave_select_channels",      case_interleave_select_channels},
    {"interleave_store_stride3",        case_interleave_store_stride3},
    {"interleave_multiway_select",      case_interleave_multiway_select},
    {"shuffle_concat",                  case_shuffle_concat},
    {"shuffle_slice_stride3",           case_shuffle_slice_stride3},
    {"shuffle_reverse",                 case_shuffle_reverse},
    {"vector_reduce_horizontal",        case_vector_reduce_horizontal},
    {"local_sum_blur_2d",               case_local_sum_blur_2d},
    {"narrow_widen_cast_chain",         case_narrow_widen_cast_chain},
    {"reinterpret_cast",                case_reinterpret_cast},
    {"predicated_guardwithif",          case_predicated_guardwithif},
    {"predicated_tailstrategy_predicate", case_predicated_tailstrategy_predicate},
    {"boundary_clamp_stencil",          case_boundary_clamp_stencil},
    {"nested_split_vectorize_unroll",   case_nested_split_vectorize_unroll},
    {"odd_width_vectorize_5",           case_odd_width_vectorize_5},
    {"odd_width_vectorize_3",           case_odd_width_vectorize_3},
    {"2d_tile_vectorize",               case_2d_tile_vectorize},
    {"gather_lut",                      case_gather_lut},
    {"tuple_output_vectorize",          case_tuple_output_vectorize},
    {"complex_nested_split_fuse_vectorize", case_complex_nested_split_fuse_vectorize},
    {"traced_vectorize",                case_traced_vectorize},
};
// clang-format on

}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    printf("[SKIP] Windows does not have a working setenv\n");
    return 0;
#else
    const int lengths[] = {1, 2, 3, 4, 5, 7, 8, 16};

    for (int max_lanes : lengths) {
        setenv("HL_FORCE_VECTOR_LEGALIZATION", std::to_string(max_lanes).c_str(), 1);
        for (const Case &c : cases) {
            bool ok;
            try {
                ok = c.fn();
            } catch (const std::exception &e) {
                printf("EXCEPTION in %s (HL_FORCE_VECTOR_LEGALIZATION=%d): %s\n",
                       c.name, max_lanes, e.what());
                ok = false;
            }
            report(c.name, max_lanes, ok);
        }
    }

    unsetenv("HL_FORCE_VECTOR_LEGALIZATION");

    if (num_failures > 0) {
        printf("%d case/length combinations failed.\n", num_failures);
        return 1;
    }

    printf("Success!\n");
    return 0;
#endif
}
