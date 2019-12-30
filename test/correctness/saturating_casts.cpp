#include "Halide.h"
#include <iostream>
#include <limits>
#include <stdio.h>

// Disable a warning in MSVC that we know will be triggered here.
#ifdef _MSC_VER
#pragma warning(disable : 4756)  // "overflow in constant arithmetic"
#endif

using namespace Halide;
using namespace Halide::ConciseCasts;

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

    Buffer<target_t> result = f.realize(7);

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

            if (simpler_correct_result != (int64_t)correct_result) {
                std::cout << "Simpler verification failed for index " << i
                          << " correct_result is " << correct_result
                          << " correct_result casted to int64_t is " << (int64_t)correct_result
                          << " simpler_correct_result is " << simpler_correct_result << "\n";
                std::cout << "in(i) " << in(i)
                          << " target_min " << target_min
                          << " target_max " << target_max << "\n";
            }
            assert(simpler_correct_result == (int64_t)correct_result);
        }

        if (result(i) != correct_result) {
            std::cout << "Match failure at index " << i
                      << " got " << result(i)
                      << " expected " << correct_result
                      << " for input " << in(i) << std::endl;
        }

        assert(result(i) == correct_result);
    }
}

template<typename source_t, typename target_t>
void test_concise(cast_maker_t cast_maker, bool saturating) {
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

    Buffer<target_t> result = f.realize(7);

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

                if (simpler_correct_result != (int64_t)correct_result) {
                    std::cout << "Simpler verification failed for index " << i
                              << " correct_result is " << correct_result
                              << " correct_result casted to int64_t is " << (int64_t)correct_result
                              << " simpler_correct_result is " << simpler_correct_result << "\n";
                    std::cout << "in(i) " << in(i)
                              << " target_min " << target_min
                              << " target_max " << target_max << "\n";
                }
                assert(simpler_correct_result == (int64_t)correct_result);
            }

        } else {
            correct_result = (target_t)in(i);
        }

        if (result(i) != correct_result) {
            std::cout << "Match failure at index " << i
                      << " got " << result(i)
                      << " expected " << correct_result
                      << " for input " << in(i)
                      << (saturating ? " saturating" : " nonsaturating") << std::endl;
        }

        assert(result(i) == correct_result);
    }
}

template<typename source_t>
void test_one_source_saturating() {
    test_saturating<source_t, int8_t>();
    test_saturating<source_t, uint8_t>();

    test_saturating<source_t, int16_t>();
    test_saturating<source_t, uint16_t>();

    test_saturating<source_t, int32_t>();
    test_saturating<source_t, uint32_t>();

    test_saturating<source_t, int64_t>();
    test_saturating<source_t, uint64_t>();

    test_saturating<source_t, float>();
    test_saturating<source_t, double>();
}

template<typename source_t>
void test_one_source_concise() {
    test_concise<source_t, int8_t>(i8, false);
    test_concise<source_t, uint8_t>(u8, false);
    test_concise<source_t, int8_t>(i8_sat, true);
    test_concise<source_t, uint8_t>(u8_sat, true);

    test_concise<source_t, int16_t>(i16, false);
    test_concise<source_t, uint16_t>(u16, false);
    test_concise<source_t, int16_t>(i16_sat, true);
    test_concise<source_t, uint16_t>(u16_sat, true);

    test_concise<source_t, int32_t>(i32, false);
    test_concise<source_t, uint32_t>(u32, false);
    test_concise<source_t, int32_t>(i32_sat, true);
    test_concise<source_t, uint32_t>(u32_sat, true);

    test_concise<source_t, int64_t>(i64, false);
    test_concise<source_t, uint64_t>(u64, false);
    test_concise<source_t, int64_t>(i64_sat, true);
    test_concise<source_t, uint64_t>(u64_sat, true);
}

template<typename source_t>
void test_one_source() {
    test_one_source_saturating<source_t>();
    test_one_source_concise<source_t>();
}

int main(int argc, char **argv) {
    test_one_source<int8_t>();
    test_one_source<uint8_t>();
    test_one_source<int16_t>();
    test_one_source<uint16_t>();
    test_one_source<int32_t>();
    test_one_source<uint32_t>();
    test_one_source<int64_t>();
    test_one_source<uint64_t>();

    // Casting out of range values from floating-point
    // to integer types is undefined behavior so only
    // do the saturating casts.
    test_one_source_saturating<float>();
    test_one_source_saturating<double>();

    printf("Success!\n");
    return 0;
}
