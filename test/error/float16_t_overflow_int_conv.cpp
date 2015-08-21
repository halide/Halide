#include "Halide.h"
#include <stdint.h>
#include <stdio.h>
#include <cmath>

using namespace Halide;

// FIXME: Why aren't we using a unit test framework for this?
// See Issue #898
void h_assert(bool condition, const char* msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    // Number is larger than can be represented in half and won't be rounded
    // down to the largest representable value in half(65504).  but should be
    // representable in single precision
    const int32_t largeNum = 65536;

    // This should fail as it triggers overflow
    float16_t fail = float16_t::make_from_signed_int(largeNum, RoundingMode::ToNearestTiesToEven);

    // Supress -Wunused-but-set-variable
    fail.is_infinity();

    printf("Should not be reached!\n");
    return 0;
}
