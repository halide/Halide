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

    halide_buffer_t buf = {0};
    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    buf.host = c;
    buf.type = UInt(8);
    buf.dimensions = 3;
    buf.dim = shape;

    identity_uint8.set_error_handler(&halide_error);

    Buffer output_buf(&buf);
    Target t = get_jit_target_from_environment();

    if (t.bits != 32) {
        identity_uint8.compile_jit(t.with_feature(Target::LargeBuffers));
        identity_uint8.realize(output_buf);
        assert(!error_occurred);

        Image<uint8_t> output = output_buf;
        assert(output(0, 0, 0) == 42);
        assert(output(output.dim(0).extent() - 1,
                      output.dim(1).extent() - 1,
                      output.dim(2).extent() - 1) == 42);
    }

    identity_uint8.compile_jit(t);
    identity_uint8.realize(output_buf);
    assert(error_occurred);

    printf("Success!\n");
}
