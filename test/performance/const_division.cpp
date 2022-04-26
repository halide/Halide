#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdint>
#include <cstdio>
#include <random>

using namespace Halide;
using namespace Halide::Tools;

// Use std::mt19937 instead of rand() to ensure consistent behavior on all systems.
// Note that this returns an unsigned result of at-least-32 bits.
std::mt19937 rng(0);

template<typename T>
bool test(int w, bool div, bool round_to_zero) {
    Func f, g, h;
    Var x, y;

    size_t bits = sizeof(T) * 8;
    bool is_signed = (T)(-1) < (T)(0);

    printf("%sInt(%2d, %2d)    ",
           is_signed ? " " : "U",
           (int)bits, w);

    int min_val = 2, num_vals = 254;

    if (bits <= 8 && is_signed) {
        // There are two types of integer division that cause runtime crashes:
        // 1) Division by zero
        // 2) Division of the smallest negative number by -1 (because
        // the result overflows)
        // In either case, let's avoid overflows to dodge such errors.
        num_vals = 126;
    }

    Buffer<T> input(w, num_vals);

    for (int y = 0; y < num_vals; y++) {
        for (int x = 0; x < input.width(); x++) {
            uint32_t bits = (uint32_t)rng();
            input(x, y) = (T)bits;
            // Round-to-zero faults on zero denominators
            if (round_to_zero && (input(x, y) == 0)) {
                input(x, y) = 1;
            }
        }
    }

    if (div) {
        if (round_to_zero) {
            // Test div. We'll unroll entirely across y to turn the denominator into a constant.
            f(x, y) = div_round_to_zero(input(x, y), cast<T>(y + min_val));

            // Reference good version. Not unrolled across y.
            g(x, y) = div_round_to_zero(input(x, y), cast<T>(y + min_val));

            // Version that uses fast_integer_divide
            h(x, y) = Halide::fast_integer_divide_round_to_zero(input(x, y), cast<uint8_t>(y + min_val));
        } else {
            // Test div
            f(x, y) = input(x, y) / cast<T>(y + min_val);

            // Reference good version
            g(x, y) = input(x, y) / cast<T>(y + min_val);

            // Version that uses fast_integer_divide
            h(x, y) = Halide::fast_integer_divide(input(x, y), cast<uint8_t>(y + min_val));
        }
    } else {
        // Test mod
        f(x, y) = input(x, y) % cast<T>(y + min_val);

        // Reference good version
        g(x, y) = input(x, y) % cast<T>(y + min_val);

        // Version that uses fast_integer_modulo
        h(x, y) = Halide::fast_integer_modulo(input(x, y), cast<uint8_t>(y + min_val));
    }

    // Try dividing by all the known constants using vectors
    f.bound(y, 0, num_vals).bound(x, 0, input.width()).unroll(y);
    h.bound(x, 0, input.width());
    if (w > 1) {
        f.vectorize(x);
        h.vectorize(x);
    }
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::DisableLLVMLoopOpt);
    f.compile_jit(t);
    g.compile_jit(t);
    h.compile_jit(t);

    Buffer<T> correct = g.realize({input.width(), num_vals});
    double t_correct = benchmark([&]() { g.realize(correct); });

    Buffer<T> fast = f.realize({input.width(), num_vals});
    double t_fast = benchmark([&]() { f.realize(fast); });

    Buffer<T> fast_dynamic = h.realize({input.width(), num_vals});
    double t_fast_dynamic = benchmark([&]() { h.realize(fast_dynamic); });

    printf("%6.3f                  %6.3f\n", t_correct / t_fast, t_correct / t_fast_dynamic);

    for (int y = 0; y < num_vals; y++) {
        for (int x = 0; x < input.width(); x++) {
            if (fast(x, y) != correct(x, y)) {
                printf("fast(%d, %d) = %lld instead of %lld (%lld/%d)\n",
                       x, y,
                       (long long int)fast(x, y),
                       (long long int)correct(x, y),
                       (long long int)input(x, y),
                       (T)(y + min_val));
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    int seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(seed);
    std::cout << "const_division test seed: " << seed << std::endl;

    bool success = true;
    for (int i = 0; i < 3; i++) {
        switch (i) {
        case 0:
            printf("division rounding to negative infinity:\n");
            break;
        case 1:
            printf("signed division rounding to zero:\n");
            break;
        case 2:
            printf("modulus:\n");
            break;
        }
        printf("type            const-divisor speed-up  runtime-divisor speed-up\n");

        // Scalar
        success = success && test<int32_t>(1, i == 0, i == 1);
        success = success && test<int16_t>(1, i == 0, i == 1);
        success = success && test<int8_t>(1, i == 0, i == 1);
        if (i != 1) {
            success = success && test<uint32_t>(1, i == 0, false);
            success = success && test<uint16_t>(1, i == 0, false);
            success = success && test<uint8_t>(1, i == 0, false);
        }

        // Vector
        success = success && test<int32_t>(8, i == 0, i == 1);
        success = success && test<int16_t>(16, i == 0, i == 1);
        success = success && test<int8_t>(32, i == 0, i == 1);
        if (i != 1) {
            success = success && test<uint32_t>(8, i == 0, false);
            success = success && test<uint16_t>(16, i == 0, false);
            success = success && test<uint8_t>(32, i == 0, false);
        }
    }

    if (!success) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
