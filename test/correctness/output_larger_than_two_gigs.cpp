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
    Var x, y, z;
    Func identity_uint8;
    identity_uint8(x, y, z) = cast<uint8_t>(42);

    uint8_t c[4096];
    memset(c, 99, sizeof(c));

    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    Buffer<uint8_t> output(c, 3, shape);

    identity_uint8.set_error_handler(&halide_error);

    Target t = get_jit_target_from_environment();

    if (t.bits != 32) {
        identity_uint8.compile_jit(t.with_feature(Target::LargeBuffers));
        identity_uint8.realize(output);
        assert(!error_occurred);

        assert(output(0, 0, 0) == 42);
        assert(output(output.extent(0) - 1, output.extent(1) - 1, output.extent(2) - 1) == 42);
    }

    identity_uint8.compile_jit(t);
    identity_uint8.realize(output);
    assert(error_occurred);

    printf("Success!\n");
}
