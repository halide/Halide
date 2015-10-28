#include "Halide.h"
#include <stdio.h>
#include <memory>

int error_occurred = false;
void halide_error(void *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

using namespace Halide;

int main(int argc, char **argv) {
    Param<int> extent;
    Var x, y, z;
    RDom r(0, extent, 0, 4096, 0, 256);
    Func big;
    big(x, y, z) = cast<uint8_t>(42);
    big.compute_root();

    Func grand_total;
    grand_total() = cast<uint64_t>(sum(cast<uint64_t>(big(r.x, r.y, r.z))));
    grand_total.set_error_handler(&halide_error);

    extent.set(4096);

    Image<uint64_t> result = grand_total.realize();

    Target t = get_jit_target_from_environment();
    if (t.bits == 64) {
        assert(!error_occurred);
        assert(result(0) == (uint64_t)4096*4096*256*42);
    } else {
        assert(error_occurred);
    }
    printf("Success!\n");
}
