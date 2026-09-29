#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("realize_constantly_larger_than_two_gigs", "is constant but exceeds 2^31 - 1.", []() {
        Var x, y, z;
        RDom r(0, 4096, 0, 4096, 0, 256);
        Func big;
        big(x, y, z) = cast<uint8_t>(42);
        big.compute_root();

        Func grand_total;
        grand_total() = cast<uint8_t>(sum(big(r.x, r.y, r.z)));

        Buffer<uint8_t> result = grand_total.realize();
        (void)result;
    });
}
