#include "Halide.h"
#include <cstdio>

using namespace Halide;

template<typename T>
T tolerance() {
    return 0;
}

template<>
float tolerance<float>() {
    return 1e-7f;
}


template<typename T>
bool equals(T a, T b, T epsilon = tolerance<T>()) {
    T error = std::abs(a - b);
    return error <= epsilon;
}

template<typename A>
bool test(int vec_width) {

    int W = vec_width * 4;
    int H = 1000;

    Buffer<A> input(W, H + 20);
    for (int y = 0; y < H + 20; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (A)((rand() & 0xffff) * 0.125 + 1.0);
        }
    }

    Var x("x"), y("y");
    Func f("f"), g("g");

    RDom r(0, W, 0, H);
    r.where((r.x * r.y) % 8 < 7);

    Expr e = input(r.x, r.y);
    for (int i = 1; i < 5; i++) {
        e = e + input(r.x, r.y + i);
    }

    for (int i = 5; i >= 0; i--) {
        e = e + input(r.x, r.y + i);
    }

    f(x, y) = cast<A>(0);
    f(r.x, r.y) = e;
    g(x, y) = cast<A>(0);
    g(r.x, r.y) = e;
    f.update(0).vectorize(r.x);

    Buffer<A> outputg = g.realize({W, H});
    Buffer<A> outputf = f.realize({W, H});

    for (int j = 0; j < H; j++) {
        for (int i = 0; i < W; i++) {
            if (!equals(outputf(i, j), outputg(i, j))) {
                std::cout << type_of<A>() << " x " << vec_width << " failed at "
                          << i << " " << j << ": "
                          << outputf(i, j) << " vs " << outputg(i, j) << "\n"
                          << "Failure!\n";
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char **argv) {
    if (!test<float>(4)) return 1;
    if (!test<float>(8)) return 1;

    printf("Success!\n");
    return 0;
}
