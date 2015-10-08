#ifndef FLOAT16_T_UPCAST_TEST_CASES_H
#define FLOAT16_T_UPCAST_TEST_CASES_H
#include <cinttypes>
#include <cmath>
#include <vector>
#include <utility>

/*
 * These test cases are the upcasting of a various constant float16_t values to
 * float and double.  They exist in a header file so multiple tests can share
 * the test cases.
 *
 * This header file might be included by runtime tests so this header file cannot
 * use anything in libHalide.
 */

struct UpCastedValue {
    float asFloat;
    double asDouble;
    static UpCastedValue make(float fValue, double dValue) {
        UpCastedValue r { .asFloat = fValue, .asDouble = dValue};
        return r;
    }
};


// Maps float16_t bit values to float and double equivalents
const std::vector<std::pair<uint16_t, UpCastedValue>> get_float16_t_upcast_test_cases() {
    std::vector<std::pair<uint16_t, UpCastedValue>> r;
    // Unfortunately we can't use the C99 hex float format because MSVC
    // doesn't support them.

    // +ve zero
    r.push_back(std::make_pair(0x0000, UpCastedValue::make(0.0f, 0.0)));
    // -ve zero
    r.push_back(std::make_pair(0x8000, UpCastedValue::make(-0.0f, -0.0)));
    // +ve infinity
    r.push_back(std::make_pair(0x7c00, UpCastedValue::make(INFINITY, (double) INFINITY)));
    // -ve infinity
    r.push_back(std::make_pair(0xfc00, UpCastedValue::make(-INFINITY, (double) -INFINITY)));
    // quiet NaN
    r.push_back(std::make_pair(0x7e00, UpCastedValue::make(
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    )));
    // Largest +ve normal number
    r.push_back(std::make_pair(0x7bff, UpCastedValue::make(65504.0f, 65504.0)));
    // Smallest -ve normal number
    r.push_back(std::make_pair(0xfbff, UpCastedValue::make(-65504.0f, -65504.0)));
    // Smallest +ve subnormal number 2^-24 (0x1.000000p-24)
    r.push_back(std::make_pair(0x0001, UpCastedValue::make((1.0f)/(1<<24), (1.0)/(1<<24))));
    // Largest -ve subnormal number -2^-24 (-0x1.000000p-24)
    r.push_back(std::make_pair(0x8001, UpCastedValue::make((-1.0f)/(1<<24), (-1.0)/(1<<24))));
    // Second smallest +ve subnormal number (0x1.000000p-23)
    r.push_back(std::make_pair(0x0002, UpCastedValue::make((1.0f)/(1<<23), (1.0)/(1<<23))));
    // Second largest -ve subnormal number (-0x1.000000p-23)
    r.push_back(std::make_pair(0x8002, UpCastedValue::make((-1.0f)/(1<<23), (-1.0)/(1<<23))));
    // Third smallest +ve subnormal number (0x1.800000p-23)
    r.push_back(std::make_pair(0x0003, UpCastedValue::make((1.5f)/(1<<23), (1.5)/(1<<23))));
    // Third largest -ve subnormal number (-0x1.800000p-23)
    r.push_back(std::make_pair(0x8003, UpCastedValue::make((-1.5f)/(1<<23), (-1.5)/(1<<23))));

    // Largest +ve subnormal (0x1.ff8000p-15)
    r.push_back(std::make_pair(0x03ff, UpCastedValue::make(
        float_from_bits(0x387fc000),
        double_from_bits(0x3f0ff80000000000)
    )));
    // Smallest -ve subnormal (-0x1.ff8000p-15)
    r.push_back(std::make_pair(0x83ff, UpCastedValue::make(
        float_from_bits(0xb87fc000),
        double_from_bits(0xbf0ff80000000000)
    )));

    // Second largest +ve subnormal (0x1.ff0000p-15)
    r.push_back(std::make_pair(0x03fe, UpCastedValue::make(
        float_from_bits(0x387f8000),
        double_from_bits(0x3f0ff00000000000)
    )));

    // 1.0
    r.push_back(std::make_pair(0x3c00, UpCastedValue::make(1.0f, 1.0)));
    // -1.0
    r.push_back(std::make_pair(0xbc00, UpCastedValue::make(-1.0f, -1.0)));

    return r;
}


#endif
