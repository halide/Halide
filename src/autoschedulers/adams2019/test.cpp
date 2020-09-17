#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    MachineParams params(32, 16000000, 40);
    // Use a fixed target for the analysis to get consistent results from this test.
    Target target("x86-64-linux-sse41-avx-avx2");

    Var x("x"), y("y");

    if (1) {
        // In a point-wise pipeline, everything should be fully fused.
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2 + 1;
        h(x, y) = g(x, y) * 2 + 1;

        h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

        Pipeline(h).auto_schedule(target, params);
    }

    if (1) {
        // In a pipeline with huge expensive stencils and low memory costs, nothing should be fused
        Func f("f"), g("g"), h("h");
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

        Pipeline(h).auto_schedule(target, params);
    }

    if (1) {
        // In a pipeline with moderate isotropic stencils, there should be some square tiling
        Func f("f"), h("h");
        f(x, y) = (x + y) * (x + 2 * y) * (x + 3 * y);
        h(x, y) = (f(x - 9, y - 9) + f(x, y - 9) + f(x + 9, y - 9) +
                   f(x - 9, y) + f(x, y) + f(x + 9, y) +
                   f(x - 9, y + 9) + f(x, y + 9) + f(x + 9, y - 9));

        h.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

        Pipeline(h).auto_schedule(target, params);
    }

    // Smaller footprint stencil -> smaller tiles
    if (1) {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2 * y) * (x + 3 * y);
        h(x, y) = (f(x - 1, y - 1) + f(x, y - 1) + f(x + 1, y - 1) +
                   f(x - 1, y) + f(x, y) + f(x + 1, y) +
                   f(x - 1, y + 1) + f(x, y + 1) + f(x + 1, y - 1));

        h.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

        Pipeline(h).auto_schedule(target, params);
    }

    // A stencil chain
    if (1) {
        const int N = 8;
        Func f[N];
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

        Pipeline(f[N - 1]).auto_schedule(target, params);
    }

    // An outer product
    if (1) {
        Buffer<float> a(2048), b(2048);
        Func f;
        f(x, y) = a(x) * b(y);

        f.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

        Pipeline(f).auto_schedule(target, params);
    }

    // A separable downsample that models the start of local_laplacian
    if (1) {
        Buffer<float> in(2048, 2048);
        Var k;
        Func orig("orig"), expensive("expensive"), downy("downy"), downx("downx");
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

        Pipeline(downx).auto_schedule(target, params);
    }

    // A Func with multiple stages, some of which include additional loops
    if (1) {
        Buffer<float> a(1024, 1024);
        Func f("multiple_stages"), g("g"), h("h");
        Var x, y;
        h(x, y) = pow(x, y);
        f(x, y) = a(x, y) * 2;
        f(x, y) += 17;
        RDom r(0, 10);
        f(x, y) += r * h(x, y);
        f(x, y) *= 2;
        f(0, y) = 23.0f;
        g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

        g.set_estimate(x, 1, 1022).set_estimate(y, 1, 1022);

        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        // A scan with pointwise stages before and after
        Buffer<float> a(1024, 1024);
        Func before[5];
        Func after[5];
        Func s("scan");
        Var x, y;
        before[0](x, y) = x + y;
        for (int i = 1; i < 5; i++) {
            before[i](x, y) = before[i - 1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = before[4](x, y);
        s(r, y) += s(r - 1, y);
        after[0](x, y) = s(y, x) + s(y, x + 100);
        for (int i = 1; i < 5; i++) {
            after[i](x, y) = after[i - 1](x, y) + 1;
        }

        after[4].set_estimate(x, 0, 1024).set_estimate(y, 0, 1024);

        Pipeline(after[4]).auto_schedule(target, params);
    }

    if (1) {
        Func f_u8("f_u8");
        Func f_u64_1("f_u64_1");
        Func f_u64_2("f_u64_2");
        Buffer<uint8_t> a(1024 * 1024 + 2);

        Var x;
        f_u8(x) = (min(a(x) + 1, 17) * a(x + 1) + a(x + 2)) * a(x) * a(x) * a(x + 1) * a(x + 1);
        f_u64_1(x) = cast<uint64_t>(f_u8(x)) + 1;
        f_u64_2(x) = f_u64_1(x) * 3;

        // Ignoring the types, it would make sense to inline
        // everything into f_64_2 but this would vectorize fairly
        // narrowly, which is a waste of work for the first Func.

        f_u64_2.set_estimate(x, 0, 1024 * 1024);

        Pipeline(f_u64_2).auto_schedule(target, params);
    }

    if (1) {
        Buffer<float> im_a(1024, 1024, "a"), im_b(1024, 1024, "b");
        im_a.fill(0.0f);
        im_b.fill(0.0f);

        Func c("c"), a("a"), b("b");
        Var i, j;
        a(j, i) = im_a(j, i);  // TODO: Add wrappers to the search space
        b(j, i) = im_b(j, i);
        RDom k(0, 1024);
        c(j, i) += a(k, i) * b(j, k);
        Func out("out");
        out(j, i) = c(j, i);

        out.set_estimate(j, 0, 1024).set_estimate(i, 0, 1024);

        Pipeline(out).auto_schedule(target, params);
    }

    if (1) {
        // A scan in x followed by a downsample in y, with pointwise stuff in between
        const int N = 3;
        Buffer<float> a(1024, 1024);
        Func p1[N], p2[N], p3[N];
        Func s("scan");
        Var x, y;
        p1[0](x, y) = x + y;
        for (int i = 1; i < N; i++) {
            p1[i](x, y) = p1[i - 1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = p1[N - 1](x, y);
        s(r, y) += s(r - 1, y);
        p2[0](x, y) = s(x, y);
        for (int i = 1; i < N; i++) {
            p2[i](x, y) = p2[i - 1](x, y) + 1;
        }
        Func down("downsample");
        down(x, y) = p2[N - 1](x, 2 * y);
        p3[0](x, y) = down(x, y);
        for (int i = 1; i < N; i++) {
            p3[i](x, y) = p3[i - 1](x, y) + 1;
        }

        p3[N - 1].set_estimate(x, 0, 1024).set_estimate(y, 0, 1024);

        Pipeline(p3[N - 1]).auto_schedule(target, params);
    }

    if (1) {
        // A gather that only uses a small portion of a potentially
        // large LUT. The number of points computed should be less
        // than points computed minimum, and the LUT should be
        // inlined, even if it's really expensive.
        Func lut("lut");
        Var x;
        lut(x) = (x + 1) * (x + 2) * (x + 3) * (x + 4) * (x + 5) * (x + 6);

        Func idx("idx");
        idx(x) = x * (10000 - x);

        Func out("out");
        out(x) = lut(clamp(idx(x), 0, 100000));

        out.set_estimate(x, 0, 10);

        Pipeline(out).auto_schedule(target, params);
    }

    if (1) {
        // A schedule where it's insane to not compute inside an rvar
        Func f("f"), g("g");
        f(x, y) = x;
        f(x, y) += 1;

        RDom r(0, 100);
        g(x, y) = 0;
        g(x, y) += f(x, 1000 * (y + r));

        g.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        // A pipeline where the vectorized dimension should alternate index
        Func f("f"), g("g"), h("h");
        f(x, y) = x * y;

        RDom r(-50, 100, -50, 100);
        g(x, y) += f(y + r.y, x + r.x);

        h(x, y) += g(y + r.y, x + r.y);

        h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

        Pipeline(h).auto_schedule(target, params);
    }

    if (1) {
        // A no-win scenario in which a Func is going to be read from
        // lots of times using a vector gather no matter how it is
        // scheduled.
        Func in("in"), a("a"), b("b");

        in(x, y) = sqrt(sqrt(sqrt(sqrt(x * y))));

        RDom r(-50, 100, -50, 100);
        a(x, y) += in(x + r.x, y + r.y);
        b(x, y) += in(y + r.y, x + r.x);

        a.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        b.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

        Pipeline({a, b}).auto_schedule(target, params);
    }

    if (1) {
        // Boring memcpy
        ImageParam im(Float(32), 2);
        Func f("f"), g("g");
        f(x, y) = im(x, y);
        g(x, y) = f(x, y);

        g.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        // A load from a tiny input image
        ImageParam im(Float(32), 2);
        Func f("f");
        f(x, y) = im(x, y) * 7;

        f.set_estimate(x, 0, 3).set_estimate(y, 0, 5);
        Pipeline(f).auto_schedule(target, params);
    }

    if (1) {
        // Lots of dimensions
        ImageParam im(Float(32), 7);
        Func f("f");
        Var z, w, t, u, v;
        f(x, y, z, w, t, u, v) = im(x, y, z, w, t, u, v) * 7;

        f.set_estimate(x, 0, 8)
            .set_estimate(y, 0, 9)
            .set_estimate(z, 0, 10)
            .set_estimate(w, 0, 5)
            .set_estimate(t, 0, 3)
            .set_estimate(u, 0, 2)
            .set_estimate(v, 0, 6);
        Pipeline(f).auto_schedule(target, params);
    }

    if (1) {
        // Long transpose chain.
        ImageParam im(Float(32), 2);
        Func f("f"), g("g"), h("h");

        f(x, y) = im(clamp(y * x, 0, 999), x);
        g(x, y) = f(clamp(y * x, 0, 999), x);
        h(x, y) = g(clamp(y * x, 0, 999), x);

        // Force everything to be compute root by accessing them in two separate outputs
        Func out1("out1"), out2("out2");
        out1(x, y) = f(x, y) + g(x, y) + h(x, y);
        out2(x, y) = f(x, y) + g(x, y) + h(x, y);

        out1.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        out2.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        Pipeline({out1, out2}).auto_schedule(target, params);
    }

    if (1) {
        ImageParam im(Float(32), 2);
        // An inlinable Func used at the start and at the end of a long stencil chain.
        const int N = 8;
        Func f[N];
        f[0] = Func("inline_me");
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

        Func g("output");
        // Access it in a way that makes it insane not to inline.
        g(x, y) = f[N - 1](x, y) + f[0](clamp(cast<int>(sin(x) * 10000), 0, 100000), clamp(cast<int>(sin(x * y) * 10000), 0, 100000));
        g.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);

        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        ;
        g() = f(3, 2);
        RDom r(0, 100);
        g() += r;
        h(x, y) = g() + x + y;

        h.set_estimate(x, 0, 1024).set_estimate(y, 0, 2048);
        Pipeline(h).auto_schedule(target, params);
    }

    if (1) {
        // Vectorizing a pure var in an update using RoundUp

        Func f("f"), g("g");

        f(x, y) = x + y;
        RDom r(0, 10);
        f(x, y) += f(x, y) * r;

        g(x, y) = f(x, y);

        g.set_estimate(x, 0, 10).set_estimate(y, 0, 2048);
        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        ImageParam im(Float(32), 2);

        // A convolution pyramid
        Func up[8], down[8];
        int sz = 2048;
        Func prev("input");
        prev(x, y) = im(x, y);

        const int N = 4;

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

        Func out;
        out(x, y) = up[0](x, y);

        out.set_estimate(x, 0, 2048).set_estimate(y, 0, 2048);
        Pipeline(out).auto_schedule(target, params);
    }

    if (1) {
        ImageParam im(Float(32), 2);

        Func f("f");
        f(x, y) = im(x, y);

        Func scan("scan");
        scan(x, y) = f(x, y);
        RDom r(1, 1999);
        scan(x, r) += scan(x, r - 1);
        scan(x, 1999 - r) += scan(x, 2000 - r);
        Func casted("casted");
        casted(x, y) = scan(x, y);

        casted.set_estimate(x, 0, 2000).set_estimate(y, 0, 2000);
        Pipeline(casted).auto_schedule(target, params);
    }

    if (1) {
        ImageParam im(Int(32), 2);

        Func f("f"), hist("hist"), output("output");
        Var i("i");
        f(x, y) = clamp(im(x, y), 0, 255);
        RDom r(0, 2000, 0, 2000);
        hist(i) = cast<uint32_t>(0);
        hist(f(r.x, r.y)) += cast<uint32_t>(1);
        output(i) = hist(i);

        f.set_estimate(x, 0, 2000).set_estimate(y, 0, 2000);
        output.set_estimate(i, 0, 256);
        Pipeline(output).auto_schedule(target, params);
    }

    return 0;
}
