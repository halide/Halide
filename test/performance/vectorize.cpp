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

DECL_SOT(uint8_t);
DECL_SOT(int8_t);
DECL_SOT(uint16_t);
DECL_SOT(int16_t);
DECL_SOT(uint32_t);
DECL_SOT(int32_t);
DECL_SOT(float);
DECL_SOT(double);

template<typename A>
bool test(int vec_width) {

    int W = vec_width*1;
    int H = 10000;

    Buffer<A> input(W, H+20);
    for (int y = 0; y < H+20; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (A)((rand() & 0xffff)*0.125 + 1.0);
        }
    }

    Var x, y;
    Func f, g;

    Expr e = input(x, y);
    for (int i = 1; i < 5; i++) {
        e = e + input(x, y+i);
    }

    for (int i = 5; i >= 0; i--) {
        e = e + input(x, y+i);
    }

    f(x, y) = e;
    g(x, y) = e;
    f.bound(x, 0, vec_width).vectorize(x);

    // Stop llvm from auto-vectorizing the scalar case and messing up
    // the comparison. Also causes cache effects, but the entire input
    // is small enough to fit in cache.
    g.reorder(y, x);

    Buffer<A> outputg = g.realize(W, H);
    Buffer<A> outputf = f.realize(W, H);

    double t_g = benchmark(10, 10, [&]() {
        g.realize(outputg);
    });
    double t_f = benchmark(10, 10, [&]() {
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

    bool ok = true;

    // Only native vector widths for now
    ok = ok && test<float>(4);
    ok = ok && test<float>(8);
    ok = ok && test<double>(2);
    ok = ok && test<uint8_t>(16);
    ok = ok && test<int8_t>(16);
    ok = ok && test<uint16_t>(8);
    ok = ok && test<int16_t>(8);
    ok = ok && test<uint32_t>(4);
    ok = ok && test<int32_t>(4);

    if (!ok) return -1;
    printf("Success!\n");
    return 0;
}
