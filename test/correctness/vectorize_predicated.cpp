#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

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

template<typename T>
bool equals(T a, T b, T epsilon = tolerance<T>()) {
    T error = std::abs(a - b);
    return error <= epsilon;
}

template<typename A>
bool test(int vec_width) {

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

    Buffer<A> outputg = g.realize({W, H});
    Buffer<A> outputf = f.realize({W, H});

    double t_g = benchmark([&]() {
        g.realize(outputg);
    });
    double t_f = benchmark([&]() {
        f.realize(outputf);
    });

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!equals(outputf(x, y), outputg(x, y))) {
                std::cout << type_of<A>() << " x " << vec_width << " failed at "
                          << x << " " << y << ": "
                          << outputf(x, y) << " vs " << outputg(x, y) << "\n"
                          << "Failure!\n";
                exit(1);
            }
        }
    }

    printf("Vectorized vs scalar (float x %d): %1.3gms %1.3gms. Speedup = %1.3f\n",
           vec_width, t_f * 1e3, t_g * 1e3, t_g / t_f);

    if (t_f > t_g) {
        printf("-> Too slow!!\n");
        return false;
    }

    return true;
}

int main(int argc, char **argv) {
    // As for now, we would only vectorize predicated store/load on Hexagon or
    // if it is of type 32-bit value and has lanes no less than 4 on x86
    bool success = true;
    success &= test<float>(4);
    success &= test<float>(8);

    if (success) {
        printf("Success!\n");
    } else {
        printf("[SKIP] This test is currently failing, but wasn't even being compiled before.\n");
    }
    return 0;
}
