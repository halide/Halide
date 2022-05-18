#include "Halide.h"
#include <algorithm>
#include <future>

#include <cstdio>

using namespace Halide;
using namespace Halide::FuncTypeChanging;

template<typename T>
static constexpr int local_bitwidth() {
    return 8 * sizeof(T);
}

template<typename T>
static T local_extract_high_bits(T val, int num_high_bits) {
    int num_low_padding_bits = local_bitwidth<T>() - num_high_bits;
    if ((unsigned)num_low_padding_bits >= (unsigned)local_bitwidth<T>()) {
        return 42;
    }
    // The sign bit is already positioned, just perform the right-shift.
    // We'll either pad with zeros (if uint) or replicate sign bit (if int).
    assert((unsigned)num_low_padding_bits < (unsigned)local_bitwidth<T>());
    return val >> num_low_padding_bits;
}

template<typename T>
static T local_variable_length_extend(T val, int num_low_bits) {
    int num_high_padding_bits = local_bitwidth<T>() - num_low_bits;
    if ((unsigned)num_high_padding_bits >= (unsigned)local_bitwidth<T>()) {
        return 42;
    }
    // First, left-shift the variable-sized input so that it's highest (sign)
    // bit is positioned in the highest (sign) bit of the containment type,
    assert((unsigned)num_high_padding_bits < (unsigned)local_bitwidth<T>());
    val <<= num_high_padding_bits;
    return local_extract_high_bits(val, /*num_high_bits=*/num_low_bits);
}

template<typename T>
static T local_extract_bits(T val, int num_low_padding_bits, int num_bits) {
    if (num_bits == 0) {
        return 42;
    }
    int num_high_padding_bits =
        (local_bitwidth<T>() - num_low_padding_bits) - num_bits;
    if ((unsigned)num_high_padding_bits >= (unsigned)local_bitwidth<T>()) {
        return 42;
    }
    // First, left-shift the variable-sized input so that it's highest (sign)
    // bit is positioned in the highest (sign) bit of the containment type,
    assert((unsigned)num_high_padding_bits < (unsigned)local_bitwidth<T>());
    val <<= num_high_padding_bits;
    return local_extract_high_bits(val, /*num_high_bits=*/num_bits);
}

template<typename T>
static T local_extract_low_bits(T val, int num_low_bits) {
    return local_extract_bits(val, /*num_low_padding_bits=*/0, num_low_bits);
}

template<typename T>
static bool expect_eq(Buffer<T> actual, Buffer<T> expected) {
    bool eq = true;
    expected.for_each_value(
        [&](const T &expected_val, const T &actual_val) {
            if (actual_val != expected_val) {
                eq = false;
                fprintf(stderr, "Failed: expected %d, actual %d\n",
                        (int)expected_val, (int)actual_val);
            }
        },
        actual);
    return eq;
}

template<typename T>
static auto gen_random_input(std::initializer_list<int> dims) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(std::numeric_limits<T>::min(),
                                                 std::numeric_limits<T>::max());

    Buffer<T> buf(dims);
    buf.for_each_value([&](T &v) { v = dist(gen); });

    return buf;
}

template<typename T>
static bool test_extract_high_bits(Target t) {
    const int width = 8192;
    Buffer<T> input_buf = gen_random_input<T>({width});

    constexpr int T_BITS = 8 * sizeof(T);
    constexpr int MAX_BITS = 2 + T_BITS;

    Var x("x");

    auto actual = [&]() {
        Buffer<T> res(MAX_BITS * width);
        Func fun("f");
        Expr input_idx = x / MAX_BITS;
        Expr num_high_bits = x % MAX_BITS;
        Expr num_low_padding_bits = local_bitwidth<T>() - num_high_bits;
        // `extract_high_bits()` is not defined for OOB or 0 num_high_bits.
        fun(x) = select(make_unsigned(num_low_padding_bits) >=
                            make_unsigned(local_bitwidth<T>()),
                        42,
                        extract_high_bits(input_buf(input_idx),
                                          make_unsigned(num_high_bits)));
        fun.realize(res, t);
        return res;
    };

    auto expected = [&]() {
        Buffer<T> res(MAX_BITS * width);
        for (int x = 0; x != res.width(); ++x) {
            int input_idx = x / MAX_BITS;
            int num_high_bits = x % MAX_BITS;
            res(x) =
                local_extract_high_bits(input_buf(input_idx), num_high_bits);
        }
        return res;
    };

    bool success = true;

    const auto res_actual = actual();
    const auto res_expected = expected();
    success &= expect_eq(res_actual, res_expected);

    return success;
}

template<typename T>
static bool test_variable_length_extend(Target t) {
    const int width = 8192;
    Buffer<T> input_buf = gen_random_input<T>({width});

    constexpr int T_BITS = 8 * sizeof(T);
    constexpr int MAX_BITS = 2 + T_BITS;

    Var x("x");

    auto actual = [&]() {
        Buffer<T> res(MAX_BITS * width);
        Func fun("f");
        Expr input_idx = x / MAX_BITS;
        Expr num_low_bits = x % MAX_BITS;
        Expr num_high_padding_bits = local_bitwidth<T>() - num_low_bits;
        // `variable_length_extend()` is not defined for OOB or 0 num_low_bits.
        fun(x) = select(make_unsigned(num_high_padding_bits) >=
                            make_unsigned(local_bitwidth<T>()),
                        42,
                        variable_length_extend(input_buf(input_idx),
                                               make_unsigned(num_low_bits)));
        fun.realize(res, t);
        return res;
    };

    auto expected = [&]() {
        Buffer<T> res(MAX_BITS * width);
        for (int x = 0; x != res.width(); ++x) {
            int input_idx = x / MAX_BITS;
            int num_low_bits = x % MAX_BITS;
            res(x) = local_variable_length_extend(input_buf(input_idx),
                                                  num_low_bits);
        }
        return res;
    };

    bool success = true;

    const auto res_actual = actual();
    const auto res_expected = expected();
    success &= expect_eq(res_actual, res_expected);

    return success;
}

template<typename T>
static bool test_extract_bits(Target t) {
    const int width = 256;
    Buffer<T> input_buf = gen_random_input<T>({width});

    constexpr int T_BITS = 8 * sizeof(T);
    constexpr int MAX_BITS = 2 + T_BITS;

    Var x("x");

    auto actual = [&]() {
        Buffer<T> res((MAX_BITS * MAX_BITS) * width);
        Func fun("f");
        Expr input_idx = x / (MAX_BITS * MAX_BITS);
        Expr num_low_padding_bits = (x / MAX_BITS) % MAX_BITS;
        Expr num_bits = x % MAX_BITS;
        Expr num_high_padding_bits =
            (local_bitwidth<T>() - num_low_padding_bits) - num_bits;
        // `extract_bits()` is not defined for 0 or OOB num_bits.
        fun(x) = select(num_bits == 0 || make_unsigned(num_high_padding_bits) >=
                                             make_unsigned(local_bitwidth<T>()),
                        42,
                        extract_bits(input_buf(input_idx),
                                     make_unsigned(num_low_padding_bits),
                                     make_unsigned(num_bits)));
        fun.realize(res, t);
        return res;
    };

    auto expected = [&]() {
        Buffer<T> res((MAX_BITS * MAX_BITS) * width);
        for (int x = 0; x != res.width(); ++x) {
            int input_idx = x / (MAX_BITS * MAX_BITS);
            int num_low_padding_bits = (x / MAX_BITS) % MAX_BITS;
            int num_bits = x % MAX_BITS;
            res(x) = local_extract_bits(input_buf(input_idx),
                                        num_low_padding_bits, num_bits);
        }
        return res;
    };

    bool success = true;

    const auto res_actual = actual();
    const auto res_expected = expected();
    success &= expect_eq(res_actual, res_expected);

    return success;
}

template<typename T>
static bool test_extract_low_bits(Target t) {
    const int width = 8192;
    Buffer<T> input_buf = gen_random_input<T>({width});

    constexpr int T_BITS = 8 * sizeof(T);
    constexpr int MAX_BITS = 2 + T_BITS;

    Var x("x");

    auto actual = [&]() {
        Buffer<T> res(MAX_BITS * width);
        Func fun("f");
        Expr input_idx = x / MAX_BITS;
        Expr num_low_bits = x % MAX_BITS;
        Expr num_high_padding_bits = local_bitwidth<T>() - num_low_bits;
        // `extract_low_bits()` is not defined for OOB or 0 num_low_bits.
        fun(x) = select(make_unsigned(num_high_padding_bits) >=
                            make_unsigned(local_bitwidth<T>()),
                        42,
                        extract_low_bits(input_buf(input_idx),
                                         make_unsigned(num_low_bits)));
        fun.realize(res, t);
        return res;
    };

    auto expected = [&]() {
        Buffer<T> res(MAX_BITS * width);
        for (int x = 0; x != res.width(); ++x) {
            int input_idx = x / MAX_BITS;
            int num_low_bits = x % MAX_BITS;
            res(x) = local_extract_low_bits(input_buf(input_idx), num_low_bits);
        }
        return res;
    };

    bool success = true;

    const auto res_actual = actual();
    const auto res_expected = expected();
    success &= expect_eq(res_actual, res_expected);

    return success;
}

template<typename T>
static bool test_with_type(Target t) {
    bool success = true;

    success &= test_extract_high_bits<T>(t);
    success &= test_variable_length_extend<T>(t);
    success &= test_extract_bits<T>(t);
    success &= test_extract_low_bits<T>(t);

    return success;
}

static bool test_all(Target t) {
    bool success = true;

    success &= test_with_type<uint8_t>(t);
    success &= test_with_type<uint16_t>(t);
    success &= test_with_type<uint32_t>(t);
    success &= test_with_type<uint64_t>(t);

    success &= test_with_type<int8_t>(t);
    success &= test_with_type<int16_t>(t);
    success &= test_with_type<int32_t>(t);
    success &= test_with_type<int64_t>(t);

    return success;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    bool success = test_all(target);

    if (!success) {
        fprintf(stderr, "Failed!\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
