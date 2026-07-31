#include "Halide.h"

#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>

using namespace Halide;

// Halide integer division rounds toward negative infinity, and modulo is the
// matching remainder (a % b == a - (a / b) * b, landing in [0, b) for b > 0).
// C++'s built-in operators truncate toward zero, so compute the reference
// results explicitly here. The denominator is always a positive integer in
// [1, 255] (it is passed to fast_integer_divide as a uint8), so we work in
// int64_t: this covers every numerator without overflow and avoids narrowing
// the divisor into the (possibly signed, possibly narrow) numerator type.
int64_t floor_div(int64_t n, int64_t d) {
    int64_t q = n / d;
    int64_t r = n % d;
    // Adjust when the truncated quotient rounded the wrong way. With d > 0 this
    // only fires for negative numerators.
    if (r != 0 && r < 0) {
        q -= 1;
    }
    return q;
}

int64_t floor_mod(int64_t n, int64_t d) {
    int64_t r = n % d;
    if (r != 0 && r < 0) {
        r += d;
    }
    return r;
}

// A spread of numerators covering the extremes of each type plus values near
// zero and a few arbitrary points in between.
template<typename T>
std::vector<T> numerator_samples() {
    const int64_t lo = (int64_t)std::numeric_limits<T>::min();
    const int64_t hi = (int64_t)std::numeric_limits<T>::max();
    const int64_t candidates[] = {
        lo, lo + 1, lo / 2, -1000000, -12345, -100, -2, -1,
        0, 1, 2, 3, 7, 100, 12345, 1000000,
        127, 128, 255, 256, 65535, 65536,
        hi / 3, hi / 2, hi - 1, hi};
    std::vector<T> samples;
    for (int64_t c : candidates) {
        if (c < lo || c > hi) {
            continue;
        }
        samples.push_back((T)c);
    }
    return samples;
}

template<typename T>
int test() {
    std::vector<T> nums = numerator_samples<T>();
    Buffer<T> numerators((int)nums.size());
    for (int i = 0; i < (int)nums.size(); i++) {
        numerators(i) = nums[i];
    }

    // Denominators run from 1 to 255. Keep them runtime-varying so that the
    // table-based path (rather than the compile-time-constant fast path) is
    // exercised.
    const int denom_extent = 255;

    Var x, y;

    Func divide;
    divide(x, y) = fast_integer_divide(numerators(x), cast<uint8_t>(y + 1));
    divide.vectorize(x, 8);
    Buffer<T> div_result = divide.realize({(int)nums.size(), denom_extent});

    Func modulo;
    modulo(x, y) = fast_integer_modulo(numerators(x), cast<uint8_t>(y + 1));
    modulo.vectorize(x, 8);
    Buffer<T> mod_result = modulo.realize({(int)nums.size(), denom_extent});

    for (int xi = 0; xi < (int)nums.size(); xi++) {
        T n = nums[xi];
        for (int yi = 0; yi < denom_extent; yi++) {
            const int64_t d = yi + 1;  // The actual (positive) uint8 divisor.

            T div_correct = (T)floor_div((int64_t)n, d);
            T div_got = div_result(xi, yi);
            if (div_got != div_correct) {
                std::cerr << "fast_integer_divide(" << (int64_t)n << ", " << d
                          << ") = " << (int64_t)div_got << " instead of "
                          << (int64_t)div_correct << "\n";
                return -1;
            }

            T mod_correct = (T)floor_mod((int64_t)n, d);
            T mod_got = mod_result(xi, yi);
            if (mod_got != mod_correct) {
                std::cerr << "fast_integer_modulo(" << (int64_t)n << ", " << d
                          << ") = " << (int64_t)mod_got << " instead of "
                          << (int64_t)mod_correct << "\n";
                return -1;
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (test<uint8_t>() != 0 ||
        test<uint16_t>() != 0 ||
        test<uint32_t>() != 0 ||
        test<int8_t>() != 0 ||
        test<int16_t>() != 0 ||
        test<int32_t>() != 0) {
        return -1;
    }
    printf("Success!\n");
    return 0;
}
