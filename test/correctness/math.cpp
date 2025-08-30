#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace Halide;

namespace {
MATCHER_P2(RelativelyNear, expected, threshold, "") {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_floating_point_v<T>) {
        if ((std::isnan(expected) && std::isnan(arg)) || arg == expected) {
            return true;
        }

        const double da = arg;
        const double db = expected;

        const double denominator = std::max(std::abs(da), std::abs(db));
        const double relative_error = denominator == 0.0 ? 0.0 : std::abs(da - db) / denominator;

        if (relative_error > threshold) {
            *result_listener << "relative error " << relative_error
                             << " exceeds threshold " << threshold
                             << " (actual=" << da << ", expected=" << db << ")";
            return false;
        }
        return true;
    } else {
        return arg == expected;
    }
}

template<typename R, typename T>
R absd(T a, T b) {
    return a < b ? b - a : a - b;
}

template<typename R, typename T>
R abs_(T a) {
    return abs(a);
}

// Note this test is more oriented toward making sure the paths
// through to math functions all work on a given target rather
// than usefully testing the accuracy of mathematical operations.
// As such little effort has been put into the domains tested,
// other than making sure they are valid for each function.

class MathTest : public testing::Test {
    Var x{"x"}, xi{"xi"};

protected:
    static constexpr double HIGH_PRECISION_THRESHOLD = 1e-6;
    static constexpr double LOW_PRECISION_THRESHOLD = 2e-3;
    static constexpr int GPU_TILE_SIZE = 16;
    static constexpr int GPU_VECTOR_SIZE = 2;

    Target target{get_jit_target_from_environment()};
    const double threshold = (target.supports_device_api(DeviceAPI::D3D12Compute) ||
                              target.supports_device_api(DeviceAPI::WebGPU)) ?
                                 LOW_PRECISION_THRESHOLD :
                                 HIGH_PRECISION_THRESHOLD;

    static constexpr int NUM_STEPS = 256;

    Func make_test_fn(const Expr &rhs) {
        Func test_fn;
        test_fn(x) = rhs;
        if (target.has_gpu_feature()) {
            test_fn.gpu_tile(x, xi, GPU_TILE_SIZE).vectorize(xi, GPU_VECTOR_SIZE);
        } else if (target.has_feature(Target::HVX)) {
            test_fn.hexagon();
        }
        return test_fn;
    }

    template<typename T>
    Buffer<T> make_in(const std::vector<std::pair<T, T>> &bounds) {
        Buffer<T> data(bounds.size(), NUM_STEPS);
        for (int i = 0; i < NUM_STEPS; i++) {
            for (int c = 0; c < bounds.size(); c++) {
                const auto &bound = bounds[c];
                data(c, i) = static_cast<T>(static_cast<double>(bound.first) +
                                            i * (static_cast<double>(bound.second) - bound.first) / NUM_STEPS);
            }
        }
        return data;
    }

    template<typename R, typename T>
    void test_case(
        Expr (*halide_fn)(Expr), R (*c_fn)(T),
        std::pair<T, T> bounds = {std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()}) {
        if (!target.supports_type(type_of<T>())) {
            GTEST_SKIP() << "Type " << type_of<T>() << " not supported";
        }

        Buffer<T> in = make_in<T>({bounds});
        Func test_fn = make_test_fn(halide_fn(in(0, x)));
        Buffer<R> result = test_fn.realize({in.height()}, target);

        for (int i = 0; i < in.height(); i++) {
            R c_result = c_fn(in(0, i));
            EXPECT_THAT(result(i), RelativelyNear(c_result, threshold));
        }
    }

    template<typename R, typename T>
    void test_case(
        Expr (*halide_fn)(Expr, Expr), R (*c_fn)(T, T),
        std::pair<T, T> bounds1 = {std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()},
        std::pair<T, T> bounds2 = {std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()}) {
        if (!target.supports_type(type_of<T>())) {
            GTEST_SKIP() << "Type " << type_of<T>() << " not supported";
        }

        Buffer<T> in = make_in<T>({bounds1, bounds2});
        Func test_fn = make_test_fn(halide_fn(in(0, x), in(1, x)));
        Buffer<R> result = test_fn.realize({in.height()}, target);

        for (int i = 0; i < in.height(); i++) {
            R c_result = c_fn(in(0, i), in(1, i));
            EXPECT_THAT(result(i), RelativelyNear(c_result, threshold));
        }
    }
};

template<typename T>
class MathTestFlt : public MathTest {
protected:
    void test_case(Expr (*halide_fn)(Expr), T (*c_fn)(T), std::pair<T, T> bounds) {
        MathTest::test_case(halide_fn, c_fn, bounds);
    }
    void test_case(Expr (*halide_fn)(Expr, Expr), T (*c_fn)(T, T), std::pair<T, T> bounds1, std::pair<T, T> bounds2) {
        MathTest::test_case(halide_fn, c_fn, bounds1, bounds2);
    }
};

using FloatTypes = testing::Types<float, double>;
TYPED_TEST_SUITE(MathTestFlt, FloatTypes);
}  // namespace

TYPED_TEST(MathTestFlt, abs) {
    this->test_case(Halide::abs, abs, {-1000, 1000});
}

TEST_F(MathTest, abs_i) {
    test_case<uint8_t, int8_t>(Halide::abs, abs_);
    test_case<uint16_t, int16_t>(Halide::abs, abs_);
    test_case<uint32_t, int32_t>(Halide::abs, abs_);
}

TYPED_TEST(MathTestFlt, absd) {
    this->test_case(Halide::absd, absd, {-25, 25}, {-25, 25});
}

TEST_F(MathTest, absd_i) {
    test_case<uint8_t, int8_t>(Halide::absd, absd);
    test_case<uint16_t, int16_t>(Halide::absd, absd);
    test_case<uint32_t, int32_t>(Halide::absd, absd);
    test_case<uint8_t, uint8_t>(Halide::absd, absd);
    test_case<uint16_t, uint16_t>(Halide::absd, absd);
    test_case<uint32_t, uint32_t>(Halide::absd, absd);
    // TODO: int64 isn't tested because the testing mechanism relies
    //   on integer types being representable with doubles.
}

TYPED_TEST(MathTestFlt, sqrt) {
    this->test_case(Halide::sqrt, sqrt, {0, 1000000});
}

TYPED_TEST(MathTestFlt, sin) {
    this->test_case(Halide::sin, sin, {5 * -M_PI, 5 * M_PI});
}

TYPED_TEST(MathTestFlt, cos) {
    this->test_case(Halide::cos, cos, {5 * -M_PI, 5 * M_PI});
}

TYPED_TEST(MathTestFlt, tan) {
    this->test_case(Halide::tan, tan, {0.49f * -M_PI, 0.49f * M_PI});
}

TYPED_TEST(MathTestFlt, asin) {
    this->test_case(Halide::asin, asin, {-1.0, 1.0});
}

TYPED_TEST(MathTestFlt, acos) {
    this->test_case(Halide::acos, acos, {-1.0, 1.0});
}

TYPED_TEST(MathTestFlt, atan) {
    this->test_case(Halide::atan, atan, {-256, 100});
}

TYPED_TEST(MathTestFlt, atan2) {
    this->test_case(Halide::atan2, atan2, {-20, 20}, {-2, 2.001f});
}

TYPED_TEST(MathTestFlt, sinh) {
    this->test_case(Halide::sinh, sinh, {5 * -M_PI, 5 * M_PI});
}

TYPED_TEST(MathTestFlt, cosh) {
    this->test_case(Halide::cosh, cosh, {0, 1});
}

TYPED_TEST(MathTestFlt, tanh) {
    this->test_case(Halide::tanh, tanh, {5 * -M_PI, 5 * M_PI});
}

TYPED_TEST(MathTestFlt, asinh) {
    this->test_case(Halide::asinh, asinh, {-10.0, 10.0});
}

TYPED_TEST(MathTestFlt, acosh) {
    this->test_case(Halide::acosh, acosh, {1.0, 10});
}

TYPED_TEST(MathTestFlt, atanh) {
    this->test_case(Halide::atanh, atanh, {-1.0, 1.0});
}

TYPED_TEST(MathTestFlt, round) {
    this->test_case(Halide::round, round, {-15, 15});
}

TYPED_TEST(MathTestFlt, exp) {
    this->test_case(Halide::exp, exp, {0, 20});
}

TYPED_TEST(MathTestFlt, log) {
    this->test_case(Halide::log, log, {1, 1000000});
}

TYPED_TEST(MathTestFlt, floor) {
    this->test_case(Halide::floor, floor, {-25, 25});
}

TYPED_TEST(MathTestFlt, ceil) {
    this->test_case(Halide::ceil, ceil, {-25, 25});
}

TYPED_TEST(MathTestFlt, trunc) {
    this->test_case(Halide::trunc, trunc, {-25, 25});
}

TYPED_TEST(MathTestFlt, pow) {
    this->test_case(Halide::pow, pow, {-10.0, 10.0}, {-4.0f, 4.0f});
}
