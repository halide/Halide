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
    // TODO: See if this can be tested somehow.
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping realize_larger_than_two_gigs test for JavaScript as executor support fails creating and array larger than 2^32 -1 bytes.\n");
        return 0;
    }

    Param<int> extent;
    Var x, y, z, w;
    RDom r(0, extent, 0, extent, 0, extent, 0, extent/2 + 1);
    Func big;
    big(x, y, z, w) = cast<uint8_t>(42);
    big.compute_root();

    Func grand_total;
    grand_total() = cast<uint8_t>(sum(big(r.x, r.y, r.z, r.w)));
    grand_total.set_error_handler(&halide_error);

    Target t = get_jit_target_from_environment();
    t.set_feature(Target::LargeBuffers);
    grand_total.compile_jit(t);

    // On large-buffer targets try an internal allocation of size just larger than 2^63
    extent.set(1 << 16);
    Buffer<uint8_t> result = grand_total.realize();
    assert(error_occurred);

    // On small-buffer targets try an internal allocation of size just larger than 2^31
    extent.set(1 << 8);
    grand_total.compile_jit(t.without_feature(Target::LargeBuffers));
    result = grand_total.realize();
    assert(error_occurred);

    printf("Success!\n");

}
