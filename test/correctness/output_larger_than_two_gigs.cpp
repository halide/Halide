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

    buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    buf.host = c;
    buf.extent[0] = 4096;
    buf.extent[1] = 4096;
    buf.extent[2] = 256;
    buf.stride[0] = 1;
    buf.stride[1] = 0;
    buf.stride[2] = 0;
    buf.elem_size = 1;

    identity_uint8.set_error_handler(&halide_error);

    Image<uint8_t> output_buf(buf);
    Target t = get_jit_target_from_environment();

    if (t.bits != 32) {
        identity_uint8.compile_jit(t.with_feature(Target::LargeBuffers));
        identity_uint8.realize(output_buf);
        assert(!error_occurred);

        Image<uint8_t> output = output_buf;
        assert(output(0, 0, 0) == 42);
        assert(output(output.extent(0) - 1, output.extent(1) - 1, output.extent(2) - 1) == 42);
    }

    identity_uint8.compile_jit(t);
    identity_uint8.realize(output_buf);
    assert(error_occurred);

    printf("Success!\n");
}
