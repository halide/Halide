#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Tools;

namespace {

template<typename T>
T tolerance() {
    return 0;
}

template<>
float tolerance<float>() {
    return 1e-7f;
}

template<>
double tolerance<double>() {
    return 1e-14;
}

template<typename A>
void test_vectorized_predicated(int vec_width) {
    int W = vec_width * 1;
    int H = 50000;

    Buffer<A> input(W, H + 20);
    for (int y = 0; y < H + 20; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (A)((rand() & 0xffff) * 0.125 + 1.0);
        }
    }

    Var x, y;
    Func f, g;

    RDom r(0, W, 0, H);
    r.where((r.x * r.y) % 8 < 7);

    Expr e = input(r.x, r.y);
    for (int i = 1; i < 5; i++) {
        e = e + input(r.x, r.y + i);
    }

    for (int i = 5; i >= 0; i--) {
        e = e + input(r.x, r.y + i);
    }

    f(x, y) = undef<A>();
    f(r.x, r.y) = e;

    g(x, y) = undef<A>();
    g(r.x, r.y) = e;

    f.update(0).vectorize(r.x);

    Buffer<A> outputf = f.realize({W, H});
    Buffer<A> outputg = g.realize({W, H});

    double t_f = benchmark([&] {
        f.realize(outputf);
    });
    double t_g = benchmark([&] {
        g.realize(outputg);
    });

    for (int yy = 0; yy < H; yy++) {
        for (int xx = 0; xx < W; xx++) {
            ASSERT_NEAR(outputf(xx, yy), outputg(xx, yy), tolerance<A>())
                << type_of<A>() << " x " << vec_width << " failed at x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_LE(t_f, t_g) << "Vectorized version is slower than scalar";
}

}  // namespace

// TODO: This test is currently failing, but wasn't even being compiled before.

TEST(VectorizePredicated, DISABLED_VectorWidth4) {
    // As for now, we would only vectorize predicated store/load on Hexagon or
    // if it is of type 32-bit value and has lanes no less than 4 on x86
    test_vectorized_predicated<float>(4);
}

TEST(VectorizePredicated, DISABLED_VectorWidth8) {
    test_vectorized_predicated<float>(8);
}
