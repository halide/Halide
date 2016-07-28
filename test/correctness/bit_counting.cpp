
#include "Halide.h"
#include <stdio.h>
#include <stdint.h>
#include <string>

using namespace Halide;

template <typename T>
T local_popcount(T v) {
    T count = 0;
    while (v) {
        if (v & 1) ++count;
        v >>= 1;
    }
    return count;
}

template <typename T>
T local_count_trailing_zeros(T v) {
    const int bits = sizeof(T)*8;
    for (T b = 0; b < bits; ++b) {
        if (v & (1 << b)) {
            // found a set bit
            return b;
        }
    }
    return 0;
}

template <typename T>
T local_count_leading_zeros(T v) {
    const int bits = sizeof(T)*8;
    for (T b = 0; b < bits; ++b) {
        if (v & (1 << (bits - 1 - b))) {
            // found a set bit
            return b;
        }
    }
    return 0;
}

template <typename T>
std::string as_bits(T v) {
    const int bits = sizeof(T)*8;
    std::string ret;
    for (int i = bits - 1; i >= 0; --i)
        ret += (v & (1 << i)) ? '1' : '0';
    return ret;
}

Var x("x");

void schedule(Func f, const Target &t) {
    // TODO: Add GPU schedule where supported.
    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon().vectorize(x, 64);
    } else {
        f.vectorize(x, 16);
    }
}

template <typename T>
int test_bit_counting(const Target &target) {
    Image<T> input(256);
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

    Image<T> popcount_result = popcount_test.realize(256);
    for (int i = 0; i < 256; ++i) {
        if (popcount_result(i) != local_popcount(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Popcount of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), popcount_result(i),
                   local_popcount(input(i)));
            return -1;
        }
    }

    Func ctlz_test("ctlz_test");
    ctlz_test(x) = count_leading_zeros(input(x));
    schedule(ctlz_test, target);

    Image<T> ctlz_result = ctlz_test.realize(256);
    for (int i = 0; i < 256; ++i) {
        if (input(i) == 0) {
            // results are undefined for zero input
            continue;
        }

        if (ctlz_result(i) != local_count_leading_zeros(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Ctlz of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), ctlz_result(i),
                   local_count_leading_zeros(input(i)));
            return -1;
        }
    }

    Func cttz_test("cttz_test");
    cttz_test(x) = count_trailing_zeros(input(x));
    schedule(cttz_test, target);

    Image<T> cttz_result = cttz_test.realize(256);
    for (int i = 0; i < 256; ++i) {
        if (input(i) == 0) {
            // results are undefined for zero input
            continue;
        }

        if (cttz_result(i) != local_count_trailing_zeros(input(i))) {
            std::string bits_string = as_bits(input(i));
            printf("Cttz of %u [0b%s] returned %d (should be %d)\n",
                   input(i), bits_string.c_str(), cttz_result(i),
                   local_count_trailing_zeros(input(i)));
            return -1;
        }
    }
    return 0;
}

int main() {
    Target target = get_jit_target_from_environment();

    if (test_bit_counting<uint16_t>(target) != 0) return -1;
    if (test_bit_counting<uint32_t>(target) != 0) return -1;

    printf("Success!\n");
    return 0;
}
