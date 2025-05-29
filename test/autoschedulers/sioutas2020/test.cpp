#include "Halide.h"

#include "test_sharding.h"

using namespace Halide;

namespace {

// Use a fixed target for the analysis to get consistent results from this test.
constexpr int hardware_parallelism = 80;
AutoschedulerParams params = {"Sioutas2020", {{"parallelism", std::to_string(hardware_parallelism)}}};
Target target("x86-64-linux-sse41-avx-avx2-cuda-cuda_capability_61");

// Reproduce issue #8557
// https://github.com/halide/Halide/issues/8557
int test_rfactor_with_split() {
    ImageParam im(Float(32), 2, "im");

    Func max_fn{"max_fn"}, sum_fn{"sum_fn"}, output{"output"};
    RDom r(0, 8192, "r");

    Var x{"x"}, y{"y"}, u{"u"};
    RVar ri{"ri"};

    max_fn(y) = Float(32).min();
    max_fn(y) = max(max_fn(y), im(r, y));

    sum_fn(y) += Halide::exp(im(r, y) - max_fn(y));
    sum_fn.update(0).split(r, r, ri, 8);
    sum_fn.update(0).rfactor(r, u);

    output(x, y) = sum_fn(x);

    output.set_estimates({{0, 8192}, {0, 32768}});
    im.set_estimates({{0, 8192}, {0, 32768}});

    const auto results = Pipeline(output).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Reproduce issue #8256
// https://github.com/halide/Halide/issues/8256
int test_rfactor_softmax() {
    ImageParam im(Float(32), 1, "im");

    Func f{"f"}, output{"output"};
    RDom r(0, 8192, "r");

    Var x{"x"}, y{"y"}, u{"u"};

    f() += fast_exp(im(r));
    f.update(0).rfactor(r, u);

    output(x) = im(x) / f();

    output.set_estimate(x, 0, 8192);
    im.set_estimates({{0, 8192}});

    const auto results = Pipeline(output).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// In a point-wise pipeline, everything should be fully fused.
int test_pointwise_fusion() {
    Func f("f"), g("g"), h("h");
    Var x{"x"}, y{"y"};

    f(x, y) = (x + y) * (x + y);
    g(x, y) = f(x, y) * 2 + 1;
    h(x, y) = g(x, y) * 2 + 1;

    h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

    const auto results = Pipeline(h).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// In a pipeline with huge expensive stencils and low memory costs, nothing should be fused
int test_huge_stencils() {
    Func f("f"), g("g"), h("h");
    Var x{"x"}, y{"y"};

    f(x, y) = (x + y) * (x + 2 * y) * (x + 3 * y) * (x + 4 * y) * (x + 5 * y);

    Expr e = 0;
    for (int i = 0; i < 100; i++) {
        e += f(x + i * 10, y + i * 10);
    }
    g(x, y) = e;

    e = 0;
    for (int i = 0; i < 100; i++) {
        e += g(x + i * 10, y + i * 10);
    }
    h(x, y) = e;

    h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

    const auto results = Pipeline(h).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// In a pipeline with moderate isotropic stencils, there should be some square tiling
int test_isotropic_stencils() {
    Func f("f"), h("h");
    Var x{"x"}, y{"y"};

    f(x, y) = (x + y) * (x + 2 * y) * (x + 3 * y);
    h(x, y) = (f(x - 9, y - 9) + f(x, y - 9) + f(x + 9, y - 9) +
               f(x - 9, y) + f(x, y) + f(x + 9, y) +
               f(x - 9, y + 9) + f(x, y + 9) + f(x + 9, y - 9));

    h.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

    const auto results = Pipeline(h).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Smaller footprint stencil -> smaller tiles
int test_small_stencils() {
    Func f("f"), g("g"), h("h");
    Var x{"x"}, y{"y"};

    f(x, y) = (x + y) * (x + 2 * y) * (x + 3 * y);
    h(x, y) = (f(x - 1, y - 1) + f(x, y - 1) + f(x + 1, y - 1) +
               f(x - 1, y) + f(x, y) + f(x + 1, y) +
               f(x - 1, y + 1) + f(x, y + 1) + f(x + 1, y - 1));

    h.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

    const auto results = Pipeline(h).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A stencil chain
int test_stencil_chain() {
    constexpr int N = 8;
    Func f[N];
    for (int i = 0; i < N; i++) {
        f[i] = Func{"f" + std::to_string(i)};
    }

    Var x{"x"}, y{"y"};

    f[0](x, y) = (x + y) * (x + 2 * y) * (x + 3 * y);
    for (int i = 1; i < N; i++) {
        Expr e = 0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                e += f[i - 1](x + dx, y + dy);
            }
        }
        f[i](x, y) = e;
    }

    f[N - 1].set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

    const auto results = Pipeline(f[N - 1]).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// An outer product
int test_outer_product() {
    Buffer<float> a(2048, "a"), b(2048, "b");
    Func f{"f"};

    Var x{"x"}, y{"y"};

    f(x, y) = a(x) * b(y);

    f.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

    const auto results = Pipeline(f).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A separable downsample that models the start of local_laplacian
int test_separable_downsample() {
    Buffer<float> in(2048, 2048, "in");

    Func orig("orig"), expensive("expensive"), downy("downy"), downx("downx");

    Var x{"x"}, y{"y"}, k{"k"};

    Expr e = 0;
    for (int i = 0; i < 100; i++) {
        e += 1;
        e *= e;
    }
    orig(x, y) = e;
    expensive(x, y, k) = orig(x, y) * orig(x, y) + (x + orig(x, y)) * (1 + orig(x, y)) + sqrt(k + orig(x, y));
    downy(x, y, k) = expensive(x, 2 * y - 1, k) + expensive(x, 2 * y, k) + expensive(x, 2 * y + 1, k) + expensive(x, 2 * y + 2, k);
    downx(x, y, k) = downy(2 * x - 1, y, k) + downy(2 * x, y, k) + downy(2 * x + 1, y, k) + downy(2 * x + 2, y, k);
    downx.set_estimate(x, 1, 1022).set_estimate(y, 1, 1022).set_estimate(k, 0, 256);

    const auto results = Pipeline(downx).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A Func with multiple stages, some of which include additional loops
int test_multiple_stages() {
    Buffer<float> a(1024, 1024, "a");

    Func f("multiple_stages"), g("g"), h("h");
    RDom r(0, 10, "r");

    Var x{"x"}, y{"y"};

    h(x, y) = pow(x, y);

    f(x, y) = a(x, y) * 2;
    f(x, y) += 17;
    f(x, y) += r * h(x, y);
    f(x, y) *= 2;
    f(0, y) = 23.0f;

    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.set_estimate(x, 1, 1022).set_estimate(y, 1, 1022);

    const auto results = Pipeline(g).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A scan with pointwise stages before and after
int test_scan_with_pointwise_stages() {
    constexpr int N = 5;

    Buffer<float> a(1024, 1024, "a");

    Func before[N];
    for (int i = 0; i < N; i++) {
        before[i] = Func{"before" + std::to_string(i)};
    }

    Func after[5];
    for (int i = 0; i < N; i++) {
        after[i] = Func{"after" + std::to_string(i)};
    }

    Func s("scan");
    RDom r(1, 1023, "r");

    Var x{"x"}, y{"y"};

    before[0](x, y) = x + y;
    for (int i = 1; i < N; i++) {
        before[i](x, y) = before[i - 1](x, y) + 1;
    }

    s(x, y) = before[N - 1](x, y);
    s(r, y) += s(r - 1, y);

    after[0](x, y) = s(y, x) + s(y, x + 100);
    for (int i = 1; i < N; i++) {
        after[i](x, y) = after[i - 1](x, y) + 1;
    }

    after[N - 1].set_estimate(x, 0, 1024).set_estimate(y, 0, 1024);

    const auto results = Pipeline(after[N - 1]).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

int test_matmul_basic() {
    Buffer<float> im_a(1024, 1024, "im_a"), im_b(1024, 1024, "im_b");

    Func c("c"), a("a"), b("b"), out("out");
    RDom k(0, 1024, "k");

    Var x{"x"}, y{"y"}, i{"i"}, j{"j"};

    im_a.fill(0.0f);
    im_b.fill(0.0f);

    a(j, i) = im_a(j, i);  // TODO: Add wrappers to the search space
    b(j, i) = im_b(j, i);
    c(j, i) += a(k, i) * b(j, k);
    out(j, i) = c(j, i);

    out.set_estimate(j, 0, 1024).set_estimate(i, 0, 1024);

    const auto results = Pipeline(out).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A scan in x followed by a downsample in y, with pointwise stuff in between
int test_scan_x_pointwise_downsample_y() {
    constexpr int N = 3;

    Buffer<float> a(1024, 1024, "a");

    Func p1[N];
    for (int i = 0; i < N; i++) {
        p1[i] = Func{"p1_" + std::to_string(i)};
    }

    Func p2[N];
    for (int i = 0; i < N; i++) {
        p2[i] = Func{"p2_" + std::to_string(i)};
    }

    Func p3[N];
    for (int i = 0; i < N; i++) {
        p3[i] = Func{"p3_" + std::to_string(i)};
    }

    Func s("scan");
    RDom r(1, 1023, "r");

    Func down("downsample");

    Var x{"x"}, y{"y"};

    p1[0](x, y) = x + y;
    for (int i = 1; i < N; i++) {
        p1[i](x, y) = p1[i - 1](x, y) + 1;
    }

    s(x, y) = p1[N - 1](x, y);
    s(r, y) += s(r - 1, y);

    p2[0](x, y) = s(x, y);
    for (int i = 1; i < N; i++) {
        p2[i](x, y) = p2[i - 1](x, y) + 1;
    }

    down(x, y) = p2[N - 1](x, 2 * y);

    p3[0](x, y) = down(x, y);
    for (int i = 1; i < N; i++) {
        p3[i](x, y) = p3[i - 1](x, y) + 1;
    }

    p3[N - 1].set_estimate(x, 0, 1024).set_estimate(y, 0, 1024);

    const auto results = Pipeline(p3[N - 1]).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A gather that only uses a small portion of a potentially
// large LUT. The number of points computed should be less
// than points computed minimum, and the LUT should be
// inlined, even if it's really expensive.
int test_gather_with_lut() {
    Func lut("lut"), idx("idx"), out("out");
    Var x{"x"}, y{"y"};

    lut(x) = (x + 1) * (x + 2) * (x + 3) * (x + 4) * (x + 5) * (x + 6);
    idx(x) = x * (10000 - x);
    out(x) = lut(clamp(idx(x), 0, 100000));

    out.set_estimate(x, 0, 10);

    const auto results = Pipeline(out).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A pipeline where the vectorized dimension should alternate index
int test_alternate_indexing() {
    Func f("f"), g("g"), h("h");
    RDom r(-50, 100, -50, 100, "r");

    Var x{"x"}, y{"y"};

    f(x, y) = x * y;
    g(x, y) += f(y + r.y, x + r.x);
    h(x, y) += g(y + r.y, x + r.y);

    h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

    const auto results = Pipeline(h).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A no-win scenario in which a Func is going to be read from
// lots of times using a vector gather no matter how it is
// scheduled.
int test_high_read_traffic() {
    Func in("in"), a("a"), b("b");
    RDom r(-50, 100, -50, 100, "r");

    Var x{"x"}, y{"y"};

    in(x, y) = sqrt(sqrt(sqrt(sqrt(x * y))));

    a(x, y) += in(x + r.x, y + r.y);
    b(x, y) += in(y + r.y, x + r.x);

    a.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
    b.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

    const auto results = Pipeline({a, b}).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Boring memcpy
int test_boring_memcpy() {
    ImageParam im(Float(32), 2, "im");
    Func f("f"), g("g");
    Var x{"x"}, y{"y"};

    f(x, y) = im(x, y);
    g(x, y) = f(x, y);

    g.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
    im.set_estimates({{0, 1000}, {0, 1000}});

    const auto results = Pipeline(g).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// A load from a tiny input image
int test_tiny_loads() {
    ImageParam im(Float(32), 2, "im");
    Func f("f");
    Var x{"x"}, y{"y"};

    f(x, y) = im(x, y) * 7;

    f.set_estimate(x, 0, 3).set_estimate(y, 0, 5);
    im.set_estimates({{0, 3}, {0, 5}});

    const auto results = Pipeline(f).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Lots of dimensions
int test_many_dimensions() {
    ImageParam im(Float(32), 7, "im");
    Func f("f");

    Var x{"x"}, y{"y"}, z{"z"}, w{"w"}, t{"t"}, u{"u"}, v{"v"};

    f(x, y, z, w, t, u, v) = im(x, y, z, w, t, u, v) * 7;

    f.set_estimate(x, 0, 8)
        .set_estimate(y, 0, 9)
        .set_estimate(z, 0, 10)
        .set_estimate(w, 0, 5)
        .set_estimate(t, 0, 3)
        .set_estimate(u, 0, 2)
        .set_estimate(v, 0, 6);

    im.set_estimates({
        {0, 8},
        {0, 9},
        {0, 10},
        {0, 5},
        {0, 3},
        {0, 2},
        {0, 6},
    });

    const auto results = Pipeline(f).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Long transpose chain.
int test_long_transpose_chain() {
    ImageParam im(Float(32), 2, "im");
    Func f("f"), g("g"), h("h"), out1("out1"), out2("out2");
    Var x{"x"}, y{"y"};

    f(x, y) = im(clamp(y * x, 0, 999), x);
    g(x, y) = f(clamp(y * x, 0, 999), x);
    h(x, y) = g(clamp(y * x, 0, 999), x);

    // Force everything to be compute root by accessing them in two separate outputs
    out1(x, y) = f(x, y) + g(x, y) + h(x, y);
    out2(x, y) = f(x, y) + g(x, y) + h(x, y);

    out1.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
    out2.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
    im.set_estimates({{0, 1000}, {0, 1000}});

    const auto results = Pipeline({out1, out2}).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// An inlinable Func used at the start and at the end of a long stencil chain.
int test_func_that_should_be_recomputed() {
    constexpr int N = 8;
    ImageParam im(Float(32), 2, "im");

    Func f[N];
    f[0] = Func("inline_me");
    for (int i = 1; i < N; i++) {
        f[i] = Func("f" + std::to_string(i));
    }

    Func g("output");

    Var x{"x"}, y{"y"};

    f[0](x, y) = im(x, y);  // inline me!
    for (int i = 1; i < N; i++) {
        Expr e = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                e += f[i - 1](x + dx, y + dy);
            }
        }
        f[i](x, y) = e;
    }

    // Access it in a way that makes it insane not to inline.
    g(x, y) = f[N - 1](x, y) + f[0](clamp(cast<int>(sin(x) * 10000), 0, 100000), clamp(cast<int>(sin(x * y) * 10000), 0, 100000));

    g.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);
    im.set_estimates({{-N, 2048 + N}, {-N, 2048 + N}});

    const auto results = Pipeline(g).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Vectorizing a pure var in an update using RoundUp
int test_roundup_in_update_stage() {
    Func f("f"), g("g");
    RDom r(0, 10, "r");

    Var x{"x"}, y{"y"};

    f(x, y) = x + y;
    f(x, y) += f(x, y) * r;

    g(x, y) = f(x, y);

    g.set_estimate(x, 0, 10).set_estimate(y, 0, 2048);

    const auto results = Pipeline(g).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

int test_convolution_pyramid() {
    constexpr int N = 4;

    ImageParam im(Float(32), 2, "im");

    Func up[N];
    for (int i = 0; i < N; i++) {
        up[i] = Func("up" + std::to_string(i));
    }

    Func down[N];
    for (int i = 0; i < N; i++) {
        down[i] = Func("down" + std::to_string(i));
    }

    Func input{"input"};
    Func out{"out"};

    Var x{"x"}, y{"y"};

    // A convolution pyramid
    int sz = 2048;
    input(x, y) = im(x, y);

    Func prev = input;
    for (int i = 0; i < N; i++) {
        up[i] = Func("up" + std::to_string(i));
        down[i] = Func("down" + std::to_string(i));
        down[i](x, y) = prev(2 * x - 10, 2 * y - 10) + prev(2 * x + 10, 2 * y + 10);
        prev = BoundaryConditions::repeat_edge(down[i], {{0, sz}, {0, sz}});
        // prev = down[i];
        sz /= 2;
    }

    for (int i = N - 1; i >= 0; i--) {
        up[i](x, y) = prev(x / 2 + 10, y / 2 + 10) + prev(x / 2 - 10, y / 2 - 10) + down[i](x, y);
        prev = up[i];
    }

    out(x, y) = up[0](x, y);

    out.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);
    im.set_estimates({{0, 2048}, {0, 2048}});  // TODO: is this correct?

    const auto results = Pipeline(out).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

int test_bidirectional_scan() {
    ImageParam im(Float(32), 2, "im");

    Func f("f"), scan("scan"), casted("casted");
    RDom r(1, 1999, "r");

    Var x{"x"}, y{"y"};

    f(x, y) = im(x, y);

    scan(x, y) = f(x, y);
    scan(x, r) += scan(x, r - 1);
    scan(x, 1999 - r) += scan(x, 2000 - r);

    casted(x, y) = scan(x, y);

    casted.set_estimate(x, 0, 2000).set_estimate(y, 0, 2000);
    im.set_estimates({{0, 2000}, {0, 2000}});

    const auto results = Pipeline(casted).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

int test_histogram() {
    ImageParam im(Int(32), 2, "im");

    Func f("f"), hist("hist"), output("output");
    RDom r(0, 2000, 0, 2000, "r");

    Var x{"x"}, y{"y"}, i{"i"};

    f(x, y) = clamp(im(x, y), 0, 255);

    hist(i) = cast<uint32_t>(0);
    hist(f(r.x, r.y)) += cast<uint32_t>(1);

    output(i) = hist(i);

    f.set_estimate(x, 0, 2000).set_estimate(y, 0, 2000);
    output.set_estimate(i, 0, 256);
    im.set_estimates({{0, 2000}, {0, 2000}});

    const auto results = Pipeline(output).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

// Scalars with a reduction
int test_scalars_with_reduction() {
    ImageParam im(Int(32), 2, "im");

    Func f("f"), output("output");
    RDom r(0, 2000, 0, 2000, "r");

    Var x{"x"}, y{"y"};

    f() = 5;
    output() = sum(im(r.x, r.y)) + f();

    im.set_estimates({{0, 2000}, {0, 2000}});

    const auto results = Pipeline(output).apply_autoscheduler(target, params);
    debug(1) << results.schedule_source;
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2 || !strlen(argv[1])) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"test_rfactor_with_split", test_rfactor_with_split},
        {"test_rfactor_softmax", test_rfactor_softmax},
        {"test_pointwise_fusion", test_pointwise_fusion},
        {"test_huge_stencils", test_huge_stencils},
        {"test_isotropic_stencils", test_isotropic_stencils},
        {"test_small_stencils", test_small_stencils},
        {"test_stencil_chain", test_stencil_chain},
        {"test_outer_product", test_outer_product},
        {"test_separable_downsample", test_separable_downsample},
        {"test_multiple_stages", test_multiple_stages},
        {"test_scan_with_pointwise_stages", test_scan_with_pointwise_stages},
        {"test_matmul_basic", test_matmul_basic},
        {"test_scan_x_pointwise_downsample_y", test_scan_x_pointwise_downsample_y},
        {"test_gather_with_lut", test_gather_with_lut},
        {"test_alternate_indexing", test_alternate_indexing},
        {"test_high_read_traffic", test_high_read_traffic},
        {"test_boring_memcpy", test_boring_memcpy},
        {"test_tiny_loads", test_tiny_loads},
        {"test_many_dimensions", test_many_dimensions},
        {"test_long_transpose_chain", test_long_transpose_chain},
        {"test_func_that_should_be_recomputed", test_func_that_should_be_recomputed},
        {"test_roundup_in_update_stage", test_roundup_in_update_stage},
        {"test_convolution_pyramid", test_convolution_pyramid},
        {"test_bidirectional_scan", test_bidirectional_scan},
        {"test_histogram", test_histogram},
        {"test_scalars_with_reduction", test_scalars_with_reduction},
    };

#ifdef HALIDE_WITH_EXCEPTIONS
    try {
#endif
        Internal::Test::Sharder sharder;
        for (size_t t = 0; t < tasks.size(); t++) {
            if (!sharder.should_run(t)) {
                continue;
            }
            const auto &[desc, task] = tasks.at(t);
            std::cout << desc << "\n";
            if (task() != 0) {
                return 1;
            }
        }
#ifdef HALIDE_WITH_EXCEPTIONS
    } catch (::Halide::Error &err) {
        // Do *not* use user_error here (or elsewhere in this function): it
        // will throw an exception, and since there is almost certainly no
        // try/catch block in our caller, it will call std::terminate,
        // swallowing all error messages.
        std::cerr << "Unhandled exception: " << err.what() << "\n";
        return 1;
    } catch (std::exception &err) {
        std::cerr << "Unhandled exception: " << err.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unhandled exception: (unknown)\n";
        return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
