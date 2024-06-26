
#include "Halide.h"
#include <stdint.h>
#include <stdio.h>
#include <string>

using namespace Halide;

template<typename T>
T local_popcount(T v) {
    T count = 0;
    while (v) {
        if (v & 1) ++count;
        v >>= 1;
    }
    return count;
}

template<typename T>
T local_count_trailing_zeros(T v) {
    const int bits = sizeof(T) * 8;
    for (T b = 0; b < bits; ++b) {
        if (v & (1 << b)) {
            // found a set bit
            return b;
        }
    }
    return bits;
}

template<typename T>
T local_count_leading_zeros(T v) {
    const int bits = sizeof(T) * 8;
    for (T b = 0; b < bits; ++b) {
        if (v & (1 << (bits - 1 - b))) {
            // found a set bit
            return b;
        }
    }
    return bits;
}

template<typename T>
std::string as_bits(T v) {
    const int bits = sizeof(T) * 8;
    std::string ret;
    for (int i = bits - 1; i >= 0; --i)
        ret += (v & (1 << i)) ? '1' : '0';
    return ret;
}

Var x("x");

void schedule(Func f, const Target &t) {
    // TODO: Add GPU schedule where supported.
    if (t.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 16);
    }
}

template<typename T>
int test_bit_counting(const Target &target) {
    Buffer<T> input(256);
    for (int i = 0; i < 256; i++) {
        if (i < 16) {
            input(i) = i;
        } else if (i < 32) {
            input(i) = 0xffffffffUL - i;
        } else {
            input(i) = rand();
        }
    }

    Func popcount_test("popcount_test");
    popcount_test(x) = popcount(input(x));
    schedule(popcount_test, target);

    Buffer<T> popcount_result = popcount_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        if (popcount_result(i) != local_popcount(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Popcount of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), popcount_result(i),
                   local_popcount(input(i)));
            return 1;
        }
    }

    Func ctlz_test("ctlz_test");
    ctlz_test(x) = count_leading_zeros(input(x));
    schedule(ctlz_test, target);

    Buffer<T> ctlz_result = ctlz_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        if (ctlz_result(i) != local_count_leading_zeros(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Ctlz of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), ctlz_result(i),
                   local_count_leading_zeros(input(i)));
            return 1;
        }
    }

    Func cttz_test("cttz_test");
    cttz_test(x) = count_trailing_zeros(input(x));
    schedule(cttz_test, target);

    Buffer<T> cttz_result = cttz_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        if (cttz_result(i) != local_count_trailing_zeros(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Cttz of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), cttz_result(i),
                   local_count_trailing_zeros(input(i)));
            return 1;
        }
    }
    return 0;
}

int main() {
    Target target = get_jit_target_from_environment();

    if (test_bit_counting<uint16_t>(target) != 0) return 1;
    if (test_bit_counting<uint32_t>(target) != 0) return 1;

    printf("Success!\n");
    return 0;
}
