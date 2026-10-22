#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("rdom_undefined", "RDom min cannot be represented as an int32: (undefined)", []() {
        Expr undef_min, undef_extent;

        // This should assert-fail
        RDom r(undef_min, undef_min);
        (void)r;

        // Just to ensure compiler doesn't optimize-away the RDom ctor
        printf("Dimensions: %d\n", r.dimensions());
    });
}
