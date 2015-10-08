#include "HalideRuntime.h"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include "float_helpers.h"

// Get test cases
#include "float16_t_upcast_test_cases.h"

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    auto testCases = get_float16_t_upcast_test_cases();
    for (auto testCase: testCases) {
        uint16_t input = testCase.first;
        float expectedOutputAsFloat = testCase.second.asFloat;
        double expectedOutputAsDouble = testCase.second.asDouble;

        float resultAsFloat = halide_float16_bits_to_float(input);
        double resultAsDouble = halide_float16_bits_to_double(input);
        // Compare bits because NaN in not comparable
        h_assert(bits_from_float(expectedOutputAsFloat) == bits_from_float(resultAsFloat),
                 "Failed to match on convert to float");
        h_assert(bits_from_double(expectedOutputAsDouble) == bits_from_double(resultAsDouble),
                 "Failed to match on convert to double");
    }
    printf("Success!\n");
    return 0;
}
