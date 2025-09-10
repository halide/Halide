#include "Halide.h"
#include <gtest/gtest.h>

#include "type_param_helpers.h"

#include <iostream>
#include <limits>

// Disable a warning in MSVC that we know will be triggered here.
#ifdef _MSC_VER
#pragma warning(disable : 4756)  // "overflow in constant arithmetic"
#endif

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {
typedef Expr (*cast_maker_t)(Expr);

// Local alias for terseness. This must be used any time the
// cast could be a float->int conversion that might not fit in the destination.
template<typename DST, typename SRC>
DST safe_cast(SRC s) {
    return Internal::safe_numeric_cast<DST, SRC>(s);
}

template<typename source_t, typename target_t>
void test_saturating() {
    source_t source_min, source_max;
    if (std::numeric_limits<source_t>::has_infinity) {
        source_max = std::numeric_limits<source_t>::infinity();
        source_min = -source_max;
    } else {
        source_min = std::numeric_limits<source_t>::lowest();
        source_max = std::numeric_limits<source_t>::max();
    }

    target_t target_min, target_max;
    if (std::numeric_limits<target_t>::has_infinity) {
        target_max = std::numeric_limits<target_t>::infinity();
        target_min = -target_max;
    } else {
        target_min = std::numeric_limits<target_t>::lowest();
        target_max = std::numeric_limits<target_t>::max();
    }

    Buffer<source_t> in(7);
    in(0) = (source_t)0;
    in(1) = (source_t)1;
    // This can intentionally change the value if source_t is unsigned
    in(2) = (source_t)-1;
    in(3) = safe_cast<source_t>(source_max);
    in(4) = safe_cast<source_t>(source_min);
    // These two can intentionally change the value if source_t is smaller than target_t
    in(5) = safe_cast<source_t>(target_min);
    in(6) = safe_cast<source_t>(target_max);

    Var x;
    Func f;

    f(x) = saturating_cast<target_t>(in(x));

    Buffer<target_t> result = f.realize({7});

    for (int32_t i = 0; i < 7; i++) {
        bool source_signed = std::numeric_limits<source_t>::is_signed;
        bool target_signed = std::numeric_limits<target_t>::is_signed;
        bool source_floating = !std::numeric_limits<source_t>::is_integer;
        bool target_floating = !std::numeric_limits<target_t>::is_integer;

        target_t correct_result;
        if (source_floating) {
            double bounded_lower = std::max((double)in(i), (double)target_min);
            if (bounded_lower >= (double)target_max) {
                correct_result = target_max;
            } else {
                correct_result = (target_t)bounded_lower;
            }
        } else if (target_floating) {
            correct_result = (target_t)std::min((double)in(i), (double)target_max);
        } else if (source_signed == target_signed) {
            if (sizeof(source_t) > sizeof(target_t)) {
                correct_result = (target_t)std::min(std::max(in(i),
                                                             safe_cast<source_t>(target_min)),
                                                    safe_cast<source_t>(target_max));
            } else {
                correct_result = (target_t)in(i);
            }
        } else {
            if (source_signed) {
                source_t val = std::max(in(i), (source_t)0);
                if (sizeof(source_t) > sizeof(target_t)) {
                    correct_result = (target_t)std::min(val, safe_cast<source_t>(target_max));
                } else {
                    correct_result = (target_t)val;
                }
            } else {
                if (sizeof(source_t) >= sizeof(target_t)) {
                    correct_result = (target_t)std::min(in(i), safe_cast<source_t>(target_max));
                } else {  // dest is signed, but larger so unsigned source_t guaranteed to fit
                    correct_result = std::min((target_t)in(i), target_max);
                }
            }
        }

        // Do a simpler verification as well if a 64-bit int will hold everything
        if (!target_floating && (sizeof(target_t) < 8 || target_signed) &&
            !source_floating && (sizeof(source_t) < 8 || source_signed)) {
            int64_t simpler_correct_result;

            simpler_correct_result = std::min(std::max((int64_t)in(i),
                                                       (int64_t)target_min),
                                              (int64_t)target_max);
            EXPECT_EQ(simpler_correct_result, (int64_t)correct_result)
                << "in(i) " << in(i)
                << " target_min " << target_min
                << " target_max " << target_max;
        }

        EXPECT_EQ(result(i), correct_result)
            << "Match failure at index " << i
            << " got " << result(i)
            << " expected " << correct_result
            << " for input " << in(i);
    }
}

template<typename target_t>
cast_maker_t get_cast_function(bool saturating) {
    if constexpr (std::is_same_v<target_t, int8_t>) {
        return saturating ? i8_sat : i8;
    } else if constexpr (std::is_same_v<target_t, uint8_t>) {
        return saturating ? u8_sat : u8;
    } else if constexpr (std::is_same_v<target_t, int16_t>) {
        return saturating ? i16_sat : i16;
    } else if constexpr (std::is_same_v<target_t, uint16_t>) {
        return saturating ? u16_sat : u16;
    } else if constexpr (std::is_same_v<target_t, int32_t>) {
        return saturating ? i32_sat : i32;
    } else if constexpr (std::is_same_v<target_t, uint32_t>) {
        return saturating ? u32_sat : u32;
    } else if constexpr (std::is_same_v<target_t, int64_t>) {
        return saturating ? i64_sat : i64;
    } else {
        static_assert(std::is_same_v<target_t, uint64_t>, "Unsupported target type for concise cast");
        return saturating ? u64_sat : u64;
    }
}

template<typename source_t, typename target_t>
void test_concise(bool saturating) {
    cast_maker_t cast_maker = get_cast_function<target_t>(saturating);

    source_t source_min = std::numeric_limits<source_t>::min();
    source_t source_max = std::numeric_limits<source_t>::max();

    target_t target_min = std::numeric_limits<target_t>::min();
    target_t target_max = std::numeric_limits<target_t>::max();

    Buffer<source_t> in(7);
    in(0) = (source_t)0;
    in(1) = (source_t)1;
    // This can intentionally change the value if source_t is unsigned
    in(2) = (source_t)-1;
    in(3) = source_max;
    in(4) = source_min;
    // These two can intentionally change the value if source_t is smaller than target_t
    in(5) = safe_cast<source_t>(target_min);
    in(6) = safe_cast<source_t>(target_max);

    Var x;
    Func f;

    f(x) = cast_maker(in(x));

    Buffer<target_t> result = f.realize({7});

    for (int32_t i = 0; i < 7; i++) {
        bool source_signed = std::numeric_limits<source_t>::is_signed;
        bool target_signed = std::numeric_limits<target_t>::is_signed;
        bool source_floating = !std::numeric_limits<source_t>::is_integer;

        target_t correct_result;
        if (saturating) {
            if (source_floating) {
                source_t bounded_lower = std::max(in(i), safe_cast<source_t>(target_min));
                if (bounded_lower >= safe_cast<source_t>(target_max)) {
                    correct_result = target_max;
                } else {
                    correct_result = (target_t)bounded_lower;
                }
            } else if (source_signed == target_signed) {
                if (sizeof(source_t) > sizeof(target_t)) {
                    correct_result = (target_t)std::min(std::max(in(i),
                                                                 safe_cast<source_t>(target_min)),
                                                        safe_cast<source_t>(target_max));
                } else {
                    correct_result = (target_t)in(i);
                }
            } else {
                if (source_signed) {
                    source_t val = std::max(in(i), (source_t)0);
                    if (sizeof(source_t) > sizeof(target_t)) {
                        correct_result = (target_t)std::min(val, safe_cast<source_t>(target_max));
                    } else {
                        correct_result = (target_t)val;
                    }
                } else {
                    if (sizeof(source_t) >= sizeof(target_t)) {
                        correct_result = (target_t)std::min(in(i), safe_cast<source_t>(target_max));
                    } else {  // dest is signed, but larger so unsigned source_t guaranteed to fit
                        correct_result = std::min((target_t)in(i), target_max);
                    }
                }
            }

            // Do a simpler verification as well if a 64-bit int will hold everything
            if ((sizeof(target_t) < 8 || target_signed) &&
                (source_floating || (sizeof(source_t) < 8 || source_signed))) {
                int64_t simpler_correct_result;

                if (source_floating) {
                    double bounded_lower = std::max((double)in(i), (double)target_min);
                    if (bounded_lower >= (double)target_max) {
                        simpler_correct_result = target_max;
                    } else {
                        simpler_correct_result = (int64_t)bounded_lower;
                    }
                } else {
                    simpler_correct_result = std::min(std::max((int64_t)in(i),
                                                               (int64_t)target_min),
                                                      (int64_t)target_max);
                }
                EXPECT_EQ(simpler_correct_result, (int64_t)correct_result)
                    << "in(i) " << in(i)
                    << " target_min " << target_min
                    << " target_max " << target_max;
            }

        } else {
            correct_result = (target_t)in(i);
        }

        EXPECT_EQ(result(i), correct_result)
            << "Match failure at index " << i
            << " got " << result(i)
            << " expected " << correct_result
            << " for input " << in(i)
            << (saturating ? " saturating" : " nonsaturating");
    }
}
}  // namespace

class SaturatingCastTestBase : public ::testing::Test {
protected:
    void SetUp() override {
#if defined(__i386__) || defined(_M_IX86)
        GTEST_SKIP() << "Skipping test because it requires bit-exact int to float casts, "
                     << "and on i386 without SSE it is hard to guarantee that the test "
                        "binary won't use x87 instructions.";
#endif
    }
};

using AllTypes = ::testing::Types<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double>;
using IntegerTypes = ::testing::Types<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t>;

template<typename>
class SaturatingCastTest : public SaturatingCastTestBase {};
using SaturatingCastTypes = CombineTypes<AllTypes, AllTypes>;
TYPED_TEST_SUITE(SaturatingCastTest, SaturatingCastTypes);

TYPED_TEST(SaturatingCastTest, SaturatingCastTest) {
    using source_t = std::tuple_element_t<0, TypeParam>;
    using target_t = std::tuple_element_t<1, TypeParam>;
    test_saturating<source_t, target_t>();
}

template<typename>
class ConciseCastTest : public SaturatingCastTestBase {};
using ConciseCastTypes = CombineTypes<IntegerTypes, IntegerTypes>;
TYPED_TEST_SUITE(ConciseCastTest, ConciseCastTypes);

TYPED_TEST(ConciseCastTest, ConciseCastNonSaturatingTest) {
    using source_t = std::tuple_element_t<0, TypeParam>;
    using target_t = std::tuple_element_t<1, TypeParam>;
    if constexpr (std::is_floating_point_v<target_t>) {
        GTEST_SKIP() << "No concise cast function for floating-point target types";
    } else {
        test_concise<source_t, target_t>(false);
    }
}

TYPED_TEST(ConciseCastTest, ConciseCastSaturatingTest) {
    using source_t = std::tuple_element_t<0, TypeParam>;
    using target_t = std::tuple_element_t<1, TypeParam>;
    if constexpr (std::is_floating_point_v<target_t>) {
        GTEST_SKIP() << "No saturating concise cast function for floating-point target types";
    } else {
        test_concise<source_t, target_t>(true);
    }
}
