#include "Halide.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <string>

using namespace Halide;

namespace {
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

void schedule(Func f) {
    // TODO: Add GPU schedule where supported.
    static const Target t = get_jit_target_from_environment();
    if (t.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 16);
    }
}

template<typename T>
void test_bit_counting() {
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
    schedule(popcount_test);

    Buffer<T> popcount_result = popcount_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        ASSERT_EQ(popcount_result(i), local_popcount(input(i)))
            << "Popcount of " << input(i) << "[0b" << as_bits(input(i))
            << "] returned " << popcount_result(i) << " (should be "
            << local_popcount(input(i)) << ")";
    }

    Func ctlz_test("ctlz_test");
    ctlz_test(x) = count_leading_zeros(input(x));
    schedule(ctlz_test);

    Buffer<T> ctlz_result = ctlz_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        ASSERT_EQ(ctlz_result(i), local_count_leading_zeros(input(i)))
            << "Ctlz of " << input(i) << "[0b" << as_bits(input(i))
            << "] returned " << ctlz_result(i) << " (should be "
            << local_count_leading_zeros(input(i)) << ")";
    }

    Func cttz_test("cttz_test");
    cttz_test(x) = count_trailing_zeros(input(x));
    schedule(cttz_test);

    Buffer<T> cttz_result = cttz_test.realize({256});
    for (int i = 0; i < 256; ++i) {
        ASSERT_EQ(cttz_result(i), local_count_trailing_zeros(input(i)))
            << "Cttz of " << input(i) << "[0b" << as_bits(input(i))
            << "] returned " << cttz_result(i) << " (should be "
            << local_count_trailing_zeros(input(i)) << ")";
    }
}
}  // namespace

TEST(BitCountingTest, UInt16) {
    test_bit_counting<uint16_t>();
}

TEST(BitCountingTest, UInt32) {
    test_bit_counting<uint32_t>();
}
