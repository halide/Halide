#include <stdio.h>
#include "Halide.h"
#include <iostream>
#include <limits>

using namespace Halide;
using namespace Halide::ConciseCasts;

typedef Expr (*cast_maker_t)(Expr);

template <typename source_t, typename target_t>
void test(cast_maker_t cast_maker, bool saturating) {
    source_t source_min = std::numeric_limits<source_t>::min();
    source_t source_max = std::numeric_limits<source_t>::max();

    target_t target_min = std::numeric_limits<target_t>::min();
    target_t target_max = std::numeric_limits<target_t>::max();

    Image<source_t> in(7);
    in(0) = (source_t)0;
    in(1) = (source_t)1;
    // This can intentionally change the value if source_t is unsigned
    in(2) = (source_t)-1;
    in(3) = (source_t)source_max;
    in(4) = (source_t)source_min;
    // These two can intentionally change the value if source_t is smaller than target_t
    in(5) = (source_t)target_min;
    in(6) = (source_t)target_max;

    Var x;
    Func f;

    f(x) = cast_maker(in(x));

    Image<target_t> result = f.realize(7);

    for (int32_t i = 0; i < 7; i++) {
        bool source_signed = std::numeric_limits<source_t>::is_signed;
        bool target_signed = std::numeric_limits<target_t>::is_signed;

        target_t correct_result;
        if (saturating) {
            if (source_signed == target_signed) {
                if (sizeof(source_t) > sizeof(target_t)) {
                    correct_result = (target_t)std::min(std::max(in(i),
                                                                 (source_t)target_min),
                                                        (source_t)target_max);
                } else {
                  correct_result = (target_t)in(i);
                }
            } else {
                if (source_signed) {
                    source_t val = std::max(in(i), (source_t)0);
                    if (sizeof(source_t) > sizeof(target_t)) {
                        correct_result = (target_t)std::min(val, (source_t)target_max);
                    } else {
                        correct_result = (target_t)val;
                    }
                } else {
                    if (sizeof(source_t) >= sizeof(target_t)) {
                        correct_result = (target_t)std::min(in(i), (source_t)target_max);
                    } else { // dest is signed, but larger so unsigned source_t guaranteed to fit
                        correct_result = std::min((target_t)in(i), target_max);
                    }
                }
            }

            // Do a simpler verification as well if a 64-bit int will hold everything
            if ((sizeof(target_t) < 8 || target_signed) &&
                (sizeof(source_t) < 8 || source_signed)) {
                  int64_t simpler_correct_result = std::min(std::max((int64_t)in(i), (int64_t)target_min), (int64_t)target_max);
                  if (simpler_correct_result != correct_result) {
                      std::cout << "Simpler verification failed for index " << i << " correct_result is " << correct_result << " correct_result casted to int64_t is " << (int64_t)correct_result << " simpler_correct_result is " << simpler_correct_result << "\n";
                      std::cout << "in(i) " << (int)in(i) << " target_min " << (int)target_min << " target_max " << (int)target_max << "\n";
                  }
                  assert(simpler_correct_result == correct_result);
            }

        } else {
            correct_result = (target_t)in(i);
        }

        if (result(i) != correct_result) {
            std::cout << "Match failure at index " << i << " got " << (int)result(i) << " expected " << (int)correct_result << " for input " << (int)in(i) << (saturating ? " saturating" : " nonsaturating") << std::endl;
        }

        assert(result(i) == correct_result);
    }
}

template <typename source_t>
void test_one() {
    test<source_t, int8_t>(i8, false);
    test<source_t, uint8_t>(u8, false);
    test<source_t, int8_t>(i8_sat, true);
    test<source_t, uint8_t>(u8_sat, true);

    test<source_t, int16_t>(i16, false);
    test<source_t, uint16_t>(u16, false);
    test<source_t, int16_t>(i16_sat, true);
    test<source_t, uint16_t>(u16_sat, true);

    test<source_t, int32_t>(i32, false);
    test<source_t, uint32_t>(u32, false);
    test<source_t, int32_t>(i32_sat, true);
    test<source_t, uint32_t>(u32_sat, true);

    test<source_t, int64_t>(i64, false);
    test<source_t, uint64_t>(u64, false);
    test<source_t, int64_t>(i64_sat, true);
    test<source_t, uint64_t>(u64_sat, true);
}

int main(int argc, char **argv) {
    test_one<int8_t>();
    test_one<uint8_t>();
    test_one<int16_t>();
    test_one<uint16_t>();
    test_one<int32_t>();
    test_one<uint32_t>();
    test_one<int64_t>();
    test_one<uint64_t>();

    printf("Success!\n");
    return 0;
}
