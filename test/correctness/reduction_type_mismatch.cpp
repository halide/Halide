#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("reduction_type_mismatch", "Tuple element 0 of update definition has type float32, but pure definition has type uint8", []() {
        Var x;
        Func f;
        RDom dom(0, 50);

        f(x) = cast<uint8_t>(0);  // The type here...
        f(dom) += 1.0f;           // does not match the type here.

        // Should result in an error
        Buffer<float> result = f.realize({50});
        (void)result;
    });
}
