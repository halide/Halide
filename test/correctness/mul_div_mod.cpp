#include "Halide.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>

#ifdef _MSC_VER
// Silence a warning that is obscure, harmless, and painful to work around
#pragma warning(disable : 4146)  // unary minus operator applied to unsigned type, result still unsigned
#endif

using namespace Halide;
using Halide::Internal::Call;

namespace {

// Test program to check basic arithmetic.
// Pseudo-random numbers are generated and arithmetic operations performed on them.
// To ensure that the extremes of the data values are included in testing, the upper
// left corner of each matrix contains the extremes.

// The code uses 64 bit arithmetic to ensure that results are correct in 32 bits and fewer,
// even if overflow occurs.

// Dimensions of the test data, and rate of salting with extreme values (1 in SALTRATE)
#define WIDTH 1024
#define HEIGHT 1024
#define SALTRATE 50
// Portion of the test data to use for testing the simplifier
#define SWIDTH 32
#define SHEIGHT HEIGHT

// Generate poor quality pseudo random numbers.
// For reproducibility, the array indices are used as the seed for each
// number generated.  The algorithm simply multiplies the seeds by large
// primes and combines them, then multiplies by additional large primes.
// We don't want to use primes that are close to powers of 2 because they dont
// randomise the bits.
//
// unique: Use different values to get unique data in each array.
// i, j: Coordinates for which the value is being generated.
uint64_t ubits(int unique, int i, int j) {
    uint64_t mi = 982451653;  // 50 M'th prime
    uint64_t mj = 776531491;  // 40 M'th prime
    uint64_t mk = 573259391;  // 30 M'th prime
    uint64_t ml = 373587883;  // 20 M'th prime
    uint64_t mu = 275604541;  // 15 M'th prime
    // Each of the above primes is at least 10^8 i.e. at least 24 bits
    // so we are assured that the initial value computed below occupies 64 bits
    // and then the subsequent operations help ensure that every bit is affected by
    // all three inputs.

    uint64_t bits;
    bits = ((unique * mu + i) * mi + j) * mj;  // All multipliers are prime
    bits = (bits ^ (bits >> 32)) * mk;
    bits = (bits ^ (bits >> 32)) * ml;
    bits = (bits ^ (bits >> 32)) * mi;
    bits = (bits ^ (bits >> 32)) * mu;
    return bits;
}

// Template to avoid autological comparison errors when comparing unsigned values for < 0
template<typename T>
bool less_than_zero(T val) {
    if constexpr (std::is_signed_v<T>) {
        return val < 0;
    } else {
        return false;
    }
}

template<typename T>
bool is_negative_one(T val) {
    if constexpr (std::is_signed_v<T>) {
        return val == -1;
    } else {
        return false;
    }
}

template<typename T, typename BIG>
BIG maximum() {
    Type t = type_of<T>();

    if (t.is_float()) {
        return (BIG)1.0;
    }
    if (t.is_uint()) {
        uint64_t max = 0;
        max = ~max;
        if (t.bits() < 64) {
            max = (((uint64_t)1) << t.bits()) - 1;
        }
        return (BIG)max;
    }
    if (t.is_int()) {
        uint64_t umax = (((uint64_t)1) << (t.bits() - 1)) - 1;
        return (BIG)umax;
    }
    assert(0);
    return (BIG)1;
}

template<typename T, typename BIG>
BIG minimum() {
    Type t = type_of<T>();

    if (t.is_float()) {
        return (BIG)0.0;
    }
    if (t.is_uint()) {
        return (BIG)0;
    }
    if (t.is_int()) {
        uint64_t umax = (((uint64_t)1) << (t.bits() - 1)) - 1;
        BIG min = umax;
        min = -min - 1;
        return min;
    }
    assert(0);
    return (BIG)0;
}

// Construct an image for testing.
// Contents are poor quality pseudo-random numbers in the natural range for the specified type.
// The top left corner contains one of two patterns.  (Remember that first coordinate is column in Halide)
//  min  max      OR      min  max
//  min  max              max  min
// The left pattern occurs when unique is odd; the right pattern when unique is even.

template<typename T, typename BIG>
Buffer<T> init(int unique, int width, int height) {
    const Type t = type_of<T>();
    if (width < 2) {
        width = 2;
    }
    if (height < 2) {
        height = 2;
    }

    Buffer<T> result(width, height);

    if (t.is_int()) {
        // Signed integer type with specified number of bits.
        int64_t max = maximum<T, int64_t>();
        int64_t min = minimum<T, int64_t>();
        int64_t neg = (~((int64_t)0)) ^ max;  // The bits that should all be 1 for negative numbers.
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                int64_t v = (int64_t)(ubits(unique, i, j));
                if (v < 0) {
                    v |= neg;  // Make all the high bits one
                } else {
                    v &= max;
                }
                // Salting with extreme values
                int64_t vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = min;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)min;
        result(1, 0) = (T)max;
        result(0, 1) = (T)((unique & 1) ? min : max);
        result(1, 1) = (T)((unique & 1) ? max : min);
    } else if (t.is_uint()) {
        uint64_t max = maximum<T, BIG>();
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                uint64_t v = ubits(unique, i, j) & max;
                // Salting with extreme values
                uint64_t vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = 0;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)0;
        result(1, 0) = (T)max;
        result(0, 1) = (T)((unique & 1) ? 0 : max);
        result(1, 1) = (T)((unique & 1) ? max : 0);
    } else if (t.is_float()) {
        uint64_t max = (uint64_t)(-1);
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                uint64_t uv = ubits(unique, i, j);
                double v = (((double)uv) / ((double)(max))) * 2.0 - 1.0;
                // Salting with extreme values
                uint64_t vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = 1.0;
                    } else {
                        v = 0.0;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)(0.0);
        result(1, 0) = (T)(1.0);
        result(0, 1) = (T)((unique & 1) ? 0.0 : 1.0);
        result(1, 1) = (T)((unique & 1) ? 1.0 : 0.0);
    } else {
        printf("Unknown data type in init.\n");
    }

    return result;
}

enum ScheduleVariant {
    CPU,
    TiledGPU,
    Hexagon
};

std::string schedule_variant_to_string(ScheduleVariant variant) {
    switch (variant) {
    case CPU:
        return "CPU";
    case TiledGPU:
        return "TiledGPU";
    case Hexagon:
        return "Hexagon";
    }
    assert(0);
}

// Test multiplication of T1 x T2 -> RT
template<typename T1, typename T2, typename RT, typename BIG>
void mul(std::tuple<int, ScheduleVariant> param, const Target &target) {
    const auto &[vector_width, scheduling] = param;
    Type rt = type_of<RT>();

    // The parameter bits can be used to control the maximum data value.
    Buffer<T1> a = init<T1, BIG>(1, WIDTH, HEIGHT);
    Buffer<T2> b = init<T2, BIG>(2, WIDTH, HEIGHT);

    // Compute the multiplication, check that the results match.
    Func f;
    Var x, y, xi, yi;
    f(x, y) = cast(rt, a(x, y)) * cast(rt, b(x, y));
    if (vector_width > 1) {
        f.vectorize(x, vector_width);
    }
    switch (scheduling) {
    case CPU:
        break;
    case TiledGPU:
        f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        break;
    case Hexagon:
        f.compute_root().hexagon();
        break;
    }

    Buffer<RT> r = f.realize({WIDTH, HEIGHT}, target);

    for (int i = 0; i < WIDTH; i++) {
        for (int j = 0; j < HEIGHT; j++) {
            T1 ai = a(i, j);
            T2 bi = b(i, j);
            RT ri = r(i, j);
            RT correct = BIG(ai) * BIG(bi);
            EXPECT_EQ(correct, ri)
                << (int64_t)ai << "*" << (int64_t)bi << " -> " << (int64_t)ri << " != " << (int64_t)correct;

            if (i < SWIDTH && j < SHEIGHT) {
                Expr ae = cast<RT>(Expr(ai));
                Expr be = cast<RT>(Expr(bi));
                Expr re = simplify(ae * be);

                // Don't check correctness of signed integer overflow.
                if (!Call::as_intrinsic(re, {Call::signed_integer_overflow})) {
                    ASSERT_TRUE(Internal::equal(re, Expr(ri)))
                        << "Compiled a*b != simplified a*b: " << (int64_t)ai << "*" << (int64_t)bi;
                }
            }
        }
    }
}

// division tests division and mod operations.
// BIG should be uint64_t, int64_t or double as appropriate.
// T should be a type known to Halide.
template<typename T, typename BIG>
void div_mod(std::tuple<int, ScheduleVariant> param, const Target &target) {
    const auto &[vector_width, scheduling] = param;
    int i, j;
    Type t = type_of<T>();
    BIG minval = minimum<T, BIG>();

    // The parameter bits can be used to control the maximum data value.
    Buffer<T> a = init<T, BIG>(1, WIDTH, HEIGHT);
    Buffer<T> b = init<T, BIG>(2, WIDTH, HEIGHT);

    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeros from b.
    // Also, cannot divide the most negative number by -1.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i, j) == 0) {
                b(i, j) = 1;  // Replace zero with one
            }
            if (a(i, j) == minval && less_than_zero(minval) && is_negative_one(b(i, j))) {
                a(i, j) = a(i, j) + 1;  // Fix it into range.
            }
        }
    }

    // Compute division and mod, and check they satisfy the requirements of Euclidean division.
    Func f;
    Var x, y, xi, yi;
    f(x, y) = Tuple(a(x, y) / b(x, y), a(x, y) % b(x, y));  // Using Halide division operation.
    if (vector_width > 1) {
        f.vectorize(x, vector_width);
    }
    switch (scheduling) {
    case CPU:
        break;
    case TiledGPU:
        f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        break;
    case Hexagon:
        f.compute_root().hexagon();
        break;
    };

    Realization R = f.realize({WIDTH, HEIGHT}, target);
    Buffer<T> q(R[0]);
    Buffer<T> r(R[1]);

    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T ai = a(i, j);
            T bi = b(i, j);
            T qi = q(i, j);
            T ri = r(i, j);

            ASSERT_EQ(BIG(qi) * BIG(bi) + ri, ai)
                << "div_mod failure for t=" << target << ":\n"
                << "(a/b)*b + a%b != a; a, b = " << (int64_t)ai
                << ", " << (int64_t)bi
                << "; q, r = " << (int64_t)qi
                << ", " << (int64_t)ri;

            ASSERT_TRUE(0 <= ri && (t.is_min((int64_t)bi) || ri < (T)std::abs((int64_t)bi)))
                << "div_mod failure for t=" << target << ":\n"
                << "ri is not in the range [0, |b|); a, b = " << (int64_t)ai
                << ", " << (int64_t)bi
                << "; q, r = " << (int64_t)qi
                << ", " << (int64_t)ri;

            if (i < SWIDTH && j < SHEIGHT) {
                Expr ae = Expr(ai);
                Expr be = Expr(bi);
                Expr qe = simplify(ae / be);
                Expr re = simplify(ae % be);
                ASSERT_TRUE(Internal::equal(qe, Expr(qi)))
                    << "div_mod failure for t=" << target << ":\n"
                    << "Compiled a/b != simplified a/b: " << (int64_t)ai
                    << "/" << (int64_t)bi
                    << " = " << (int64_t)qi
                    << " != " << qe;
                ASSERT_TRUE(Internal::equal(re, Expr(ri)))
                    << "div_mod failure for t=" << target << ":\n"
                    << "Compiled a%b != simplified a%b: " << (int64_t)ai
                    << "%" << (int64_t)bi
                    << " = " << (int64_t)ri
                    << " != " << re;
            }
        }
    }
}

// f_mod tests floating mod operations.
// BIG should be double.
// T should be a type known to Halide.
template<typename T, typename BIG>
void f_mod() {
    int i, j;

    Buffer<T> a = init<T, BIG>(1, WIDTH, HEIGHT);
    Buffer<T> b = init<T, BIG>(2, WIDTH, HEIGHT);
    Buffer<T> out(WIDTH, HEIGHT);

    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeros from b.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i, j) == 0.0) {
                b(i, j) = 1.0;  // Replace zero with one.
            }
        }
    }

    // Compute modulus result and check it.
    Func f;
    f(_) = a(_) % b(_);  // Using Halide mod operation.
    f.realize(out);

    // Explicit checks of the simplifier for consistency with runtime computation
    for (i = 0; i < std::min(SWIDTH, WIDTH); i++) {
        for (j = 0; j < std::min(SHEIGHT, HEIGHT); j++) {
            T arg_a = a(i, j);
            T arg_b = b(i, j);
            T v = out(i, j);

            Expr in_e = simplify(cast<T>((float)arg_a) % cast<T>((float)arg_b));
            Expr out_e = simplify(cast<T>((float)v));

            const auto *in_f = in_e.as<Internal::FloatImm>();
            const auto *out_f = out_e.as<Internal::FloatImm>();

            ASSERT_NE(in_f, nullptr);
            ASSERT_NE(out_f, nullptr);
            ASSERT_EQ(in_f->value, out_f->value) << "in_e = " << in_e << ", out_e = " << out_e;
        }
    }
}

class MulDivModTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
};
class MulTest : public MulDivModTest, public ::testing::WithParamInterface<std::tuple<int, ScheduleVariant>> {};
class DivModTest : public MulDivModTest, public ::testing::WithParamInterface<std::tuple<int, ScheduleVariant>> {};
class FloatModTest : public MulDivModTest {};
}  // namespace

// Multiplication tests
TEST_P(MulTest, NonWideningUInt8) {
    mul<uint8_t, uint8_t, uint8_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, NonWideningUInt16) {
    mul<uint16_t, uint16_t, uint16_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, NonWideningUInt32) {
    mul<uint32_t, uint32_t, uint32_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, NonWideningInt8) {
    mul<int8_t, int8_t, int8_t, int64_t>(GetParam(), target);
}

TEST_P(MulTest, NonWideningInt16) {
    mul<int16_t, int16_t, int16_t, int64_t>(GetParam(), target);
}

TEST_P(MulTest, NonWideningInt32) {
    mul<int32_t, int32_t, int32_t, int64_t>(GetParam(), target);
}

TEST_P(MulTest, WideningUInt8ToUInt16) {
    mul<uint8_t, uint8_t, uint16_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, WideningUInt16ToUInt32) {
    mul<uint16_t, uint16_t, uint32_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, WideningInt8ToInt16) {
    mul<int8_t, int8_t, int16_t, int64_t>(GetParam(), target);
}

TEST_P(MulTest, WideningInt16ToInt32) {
    mul<int16_t, int16_t, int32_t, int64_t>(GetParam(), target);
}

// These aren't all the possible mixed multiplications, but they
// cover the special cases we have in Halide.

TEST_P(MulTest, MixedUInt16UInt32) {
    mul<uint16_t, uint32_t, uint32_t, uint64_t>(GetParam(), target);
}

TEST_P(MulTest, MixedInt16Int32) {
    mul<int16_t, int32_t, int32_t, int64_t>(GetParam(), target);
}

TEST_P(MulTest, MixedUInt16Int32) {
    mul<uint16_t, int32_t, int32_t, uint64_t>(GetParam(), target);
}

// Division/Modulo tests
TEST_P(DivModTest, UInt8) {
    div_mod<uint8_t, uint64_t>(GetParam(), target);
}

TEST_P(DivModTest, UInt16) {
    div_mod<uint16_t, uint64_t>(GetParam(), target);
}

TEST_P(DivModTest, UInt32) {
    div_mod<uint32_t, uint64_t>(GetParam(), target);
}

TEST_P(DivModTest, Int8) {
    div_mod<int8_t, int64_t>(GetParam(), target);
}

TEST_P(DivModTest, Int16) {
    div_mod<int16_t, int64_t>(GetParam(), target);
}

TEST_P(DivModTest, Int32) {
    div_mod<int32_t, int64_t>(GetParam(), target);
}

// Floating-point modulo test
TEST_F(FloatModTest, Float32Modulo) {
    f_mod<float, double>();
}

// Test instantiations
namespace {
Target target = get_jit_target_from_environment();
ScheduleVariant get_scheduling() {
    if (target.has_gpu_feature()) {
        return TiledGPU;
    }
    if (target.has_feature(Target::HVX)) {
        return Hexagon;
    }
    return CPU;
}
std::vector<int> get_vector_widths() {
    if (target.has_gpu_feature()) {
        return {1, 2, 4};
    }
    if (target.has_feature(Target::HVX)) {
        return {1, 128};
    }
    return {1, 2, 4, 8, 16};
}
const auto MulDivModTestParams =
    ::testing::Combine(::testing::ValuesIn(get_vector_widths()), ::testing::Values(get_scheduling()));
std::string MulDivModTestParamsToString(const ::testing::TestParamInfo<std::tuple<int, ScheduleVariant>> &info) {
    return "VectorWidth" + std::to_string(std::get<0>(info.param)) +
           "_" + schedule_variant_to_string(std::get<1>(info.param));
}
}  // namespace
INSTANTIATE_TEST_SUITE_P(VectorWidth, MulTest, MulDivModTestParams, MulDivModTestParamsToString);
INSTANTIATE_TEST_SUITE_P(VectorWidth, DivModTest, MulDivModTestParams, MulDivModTestParamsToString);
