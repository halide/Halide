#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("lerp_mismatch", "Can't lerp between uint16(0) of type uint16 and uint8(42) of different type uint8\n", []() {
        // This should trigger an error.
        Func f;
        f() = lerp(cast<uint16_t>(0), cast<uint8_t>(42), 0.5f);
    });
}
