#include "Halide.h"

#include <dlfcn.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!dlopen("auto_schedule.so", RTLD_LAZY)) {
        std::cerr << "Failed to load autoscheduler: " << dlerror() << "\n";
        return 1;
    }

    MachineParams params(32, 16000000, 40);
    // Use a fixed target for the analysis to get consistent results from this test.
    Target target("x86-64-linux-sse41-avx-avx2");

    Var x("x"), y("y");

    if (0) {
        // In a point-wise pipeline, everything should be fully fused.
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2 + 1;
        h(x, y) = g(x, y) * 2 + 1;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        Pipeline(h).auto_schedule(target, params);
    }

    if (0) {
        // In a pipeline with huge expensive stencils and low memory costs, nothing should be fused
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y) * (x + 4*y) * (x + 5*y);
        Expr e = 0;
        for (int i = 0; i < 100; i++) {
            e += f(x + i*10, y + i*10);
        }
        g(x, y) = e;
        e = 0;
        for (int i = 0; i < 100; i++) {
            e += g(x + i*10, y + i*10);
        }
        h(x, y) = e;

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        Pipeline(h).auto_schedule(target, params);
    }

    if (0) {
        // In a pipeline with moderate isotropic stencils, there should be some square tiling
        Func f("f"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = (f(x-9, y-9) + f(x, y-9) + f(x+9, y-9) +
                   f(x-9, y  ) + f(x, y  ) + f(x+9, y  ) +
                   f(x-9, y+9) + f(x, y+9) + f(x+9, y-9));


        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        Pipeline(h).auto_schedule(target, params);
    }

    // Smaller footprint stencil -> smaller tiles
    if (0) {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        h(x, y) = (f(x-1, y-1) + f(x, y-1) + f(x+1, y-1) +
                   f(x-1, y  ) + f(x, y  ) + f(x+1, y  ) +
                   f(x-1, y+1) + f(x, y+1) + f(x+1, y-1));

        h.estimate(x, 0, 2048).estimate(y, 0, 2048);

        Pipeline(h).auto_schedule(target, params);
    }

    // A stencil chain
    if (1) {
        const int N = 8;
        Func f[N];
        f[0](x, y) = (x + y) * (x + 2*y) * (x + 3*y);
        for (int i = 1; i < N; i++) {
            Expr e = 0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    e += f[i-1](x + dx, y + dy);
                }
            }
            f[i](x, y) = e;
        }
        f[N-1].estimate(x, 0, 2048).estimate(y, 0, 2048);

        Pipeline(f[N-1]).auto_schedule(target, params);
    }

    // An outer product
    if (0) {
        Buffer<float> a(2048), b(2048);
        Func f;
        f(x, y) = a(x) * b(y);

        f.estimate(x, 0, 2048).estimate(y, 0, 2048);

        Pipeline(f).auto_schedule(target, params);
    }

    // A separable downsample that models the start of local_laplacian
    if (0) {
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
        downy(x, y, k) = expensive(x, 2*y - 1, k) + expensive(x, 2*y, k) + expensive(x, 2*y+1, k) + expensive(x, 2*y + 2, k);
        downx(x, y, k) = downy(2*x-1, y, k) + downy(2*x, y, k) + downy(2*x + 1, y, k) + downy(2*x + 2, y, k);
        downx.estimate(x, 1, 1022).estimate(y, 1, 1022).estimate(k, 0, 256);

        Pipeline(downx).auto_schedule(target, params);
    }

    // A Func with multiple stages, some of which include additional loops
    if (0) {
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

        g.estimate(x, 1, 1022).estimate(y, 1, 1022);

        Pipeline(g).auto_schedule(target, params);
    }

    if (0) {
        // A scan with pointwise stages before and after
        Buffer<float> a(1024, 1024);
        Func before[5];
        Func after[5];
        Func s("scan");
        Var x, y;
        before[0](x, y) = x + y;
        for (int i = 1; i < 5; i++) {
            before[i](x, y) = before[i-1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = before[4](x, y);
        s(r, y) += s(r-1, y);
        after[0](x, y) = s(x, y);
        for (int i = 1; i < 5; i++) {
            after[i](x, y) = after[i-1](x, y) + 1;
        }

        after[4].estimate(x, 0, 1024).estimate(y, 0, 1024);

        Pipeline(after[4]).auto_schedule(target, params);
    }


    if (0) {
        Func f_u8("f_u8");
        Func f_u64_1("f_u64_1");
        Func f_u64_2("f_u64_2");
        Buffer<uint8_t> a(1024 * 1024 + 2);

        Var x;
        f_u8(x) = (min(a(x) + 1, 17) * a(x+1) + a(x+2)) * a(x) * a(x) * a(x + 1) * a(x + 1);
        f_u64_1(x) = cast<uint64_t>(f_u8(x)) + 1;
        f_u64_2(x) = f_u64_1(x) * 3;

        // Ignoring the types, it would make sense to inline
        // everything into f_64_2 but this would vectorize fairly
        // narrowly, which is a waste of work for the first Func.

        f_u64_2.estimate(x, 0, 1024 * 1024);

        Pipeline(f_u64_2).auto_schedule(target, params);
    }

    if (0) {
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

        out.estimate(j, 0, 1024).estimate(i, 0, 1024);

        Pipeline(out).auto_schedule(target, params);
    }

    if (0) {
        // A scan in x followed by a downsample in y, with pointwise stuff in between
        const int N = 3;
        Buffer<float> a(1024, 1024);
        Func p1[N], p2[N], p3[N];
        Func s("scan");
        Var x, y;
        p1[0](x, y) = x + y;
        for (int i = 1; i < N; i++) {
            p1[i](x, y) = p1[i-1](x, y) + 1;
        }
        RDom r(1, 1023);
        s(x, y) = p1[N-1](x, y);
        s(r, y) += s(r-1, y);
        p2[0](x, y) = s(x, y);
        for (int i = 1; i < N; i++) {
            p2[i](x, y) = p2[i-1](x, y) + 1;
        }
        Func down("downsample");
        down(x, y) = p2[N-1](x, 2*y);
        p3[0](x, y) = down(x, y);
        for (int i = 1; i < N; i++) {
            p3[i](x, y) = p3[i-1](x, y) + 1;
        }

        p3[N-1].estimate(x, 0, 1024).estimate(y, 0, 1024);

        Pipeline(p3[N-1]).auto_schedule(target, params);
    }

    if (0) {
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

        out.estimate(x, 0, 10);

        Pipeline(out).auto_schedule(target, params);
    }

    if (0) {
        // A schedule where it's insane to not compute inside an rvar
        Func f("f"), g("g");
        f(x, y) = x;
        f(x, y) += 1;

        RDom r(0, 100);
        g(x, y) = 0;
        g(x, y) += f(x, 1000*(y+r));

        g.estimate(x, 0, 1000).estimate(y, 0, 1000);

        Pipeline(g).auto_schedule(target, params);
    }

    if (1) {
        // A pipeline where the vectorized dimension should alternate index
        Func f("f"), g("g"), h("h");
        f(x, y) = x*y;

        RDom r(-50, 100, -50, 100);
        g(x, y) += f(y + r.y, x + r.x);

        h(x, y) += g(y + r.y, x + r.y);

        h.estimate(x, 0, 1000).estimate(y, 0, 1000);

        Pipeline(h).auto_schedule(target, params);
    }

    return 0;
}
