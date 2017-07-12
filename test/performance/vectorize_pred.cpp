#include "Halide.h"
#include <cstdio>
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

template<typename A>
const char *string_of_type();

#define DECL_SOT(name)                                          \
    template<>                                                  \
    const char *string_of_type<name>() {return #name;}

DECL_SOT(float);

template<typename A>
bool test(int vec_width) {

    int W = vec_width*1;
    int H = 50000;

    Buffer<A> input(W, H+20);
    for (int y = 0; y < H+20; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (A)((rand() & 0xffff)*0.125 + 1.0);
        }
    }

    Var x, y;
    Func f, g;

    RDom r(0, W, 0, H);
    r.where((r.x*r.y) % 8 < 7);

    Expr e = input(r.x, r.y);
    for (int i = 1; i < 5; i++) {
        e = e + input(r.x, r.y+i);
    }

    for (int i = 5; i >= 0; i--) {
        e = e + input(r.x, r.y+i);
    }

    f(x, y) = undef<A>();
    f(r.x, r.y) = e;
    g(x, y) = undef<A>();
    g(r.x, r.y) = e;
    f.update(0).vectorize(r.x);

    Buffer<A> outputg = g.realize(W, H);
    Buffer<A> outputf = f.realize(W, H);

    double t_g = benchmark(1, 10, [&]() {
        g.realize(outputg);
    });
    double t_f = benchmark(1, 10, [&]() {
        f.realize(outputf);
    });

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (outputf(x, y) != outputg(x, y)) {
                printf("%s x %d failed at %d %d: %d vs %d\n",
                       string_of_type<A>(), vec_width,
                       x, y,
                       (int)outputf(x, y),
                       (int)outputg(x, y)
                    );
                return false;
            }
        }
    }

    printf("Vectorized vs scalar (%s x %d): %1.3gms %1.3gms. Speedup = %1.3f\n",
           string_of_type<A>(), vec_width, t_f * 1e3, t_g * 1e3, t_g / t_f);

    if (t_f > t_g) {
        return false;
    }

    return true;
}

int main(int argc, char **argv) {
    // As for now, we would only vectorize predicated store/load on Hexagon or
    // if it is of type 32-bit value and has lanes no less than 4 on x86
    test<float>(4);
    test<float>(8);

    printf("Success!\n");
    return 0;
}
