#include "HalideRuntime.h"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <vector>
#include "float_helpers.h"

// Get test cases
#include "float16_t_downcast_test_cases.h"

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

void test_float(float input, DownCastedValue r) {
    uint16_t floatAsHalfRNE = halide_float_to_float16_bits(input);
    h_assert(floatAsHalfRNE == r.RNE, "Failed RNE round from float");
}

void test_double(double input, DownCastedValue r) {
    uint16_t doubleAsHalfRNE = halide_double_to_float16_bits(input);
    h_assert(doubleAsHalfRNE == r.RNE, "Failed RNE round from double");
}

int main() {
    std::pair<Float16ToFloatMap,Float16ToDoubleMap> expectedResults= get_float16_t_downcast_test_cases();
    // Test float -> float16_t conversion
    for (auto testCase : expectedResults.first) {
        float input = testCase.first;
        DownCastedValue expectedResult = testCase.second;
        test_float(input, expectedResult);
    }
    // Test double -> float16_t conversion
    for (auto testCase : expectedResults.second) {
        double input = testCase.first;
        DownCastedValue expectedResult = testCase.second;
        test_double(input, expectedResult);
    }
    printf("Success!\n");
    return 0;
}
