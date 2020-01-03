#include "Halide.h"
#include <memory>
#include <stdio.h>

int error_occurred = false;
void halide_error(void *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z;
    RDom r(0, 4096, 0, 4096, 0, 256);
    Func big;
    big(x, y, z) = cast<uint8_t>(42);
    big.set_error_handler(&halide_error);
    big.compute_root();

    Func grand_total;
    grand_total() = cast<uint8_t>(sum(big(r.x, r.y, r.z)));
    grand_total.set_error_handler(&halide_error);

    Buffer<uint8_t> result = grand_total.realize();

    assert(error_occurred);
    printf("Success!\n");
}
