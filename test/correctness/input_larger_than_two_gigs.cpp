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
        printf("Skipping input_larger_than_two_gigs test for JavaScript as executor support fails creating and array larger than 2^32 -1 bytes.\n");
        return 0;
    }

    uint8_t c[4096];
    memset(c, 42, sizeof(c));

    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    Halide::Buffer<uint8_t> buf(c, 3, shape);

    ImageParam input(UInt(8), 3);
    input.set(buf);

    Var x;
    Func grand_total;
    grand_total() = cast<uint64_t>(input(0, 0, 0) + input(input.dim(0).extent()-1, input.dim(1).extent()-1, input.dim(2).extent()-1));
    grand_total.set_error_handler(&halide_error);

    Target t = get_jit_target_from_environment();

    Buffer<uint64_t> result;
    if (t.bits != 32) {
        grand_total.compile_jit(t.with_feature(Target::LargeBuffers));
        result = grand_total.realize();
        assert(!error_occurred);
        assert(result(0) == (uint64_t)84);
    }

    grand_total.compile_jit(t);
    result = grand_total.realize();
    assert(error_occurred);

    printf("Success!\n");
}
