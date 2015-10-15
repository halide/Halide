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
    Target t = get_target_from_environment();

    Var x, y, z;
    Func identity_uint8;
    identity_uint8(x, y, z) = cast<uint8_t>(42);
    identity_uint8.set_error_handler(&halide_error);

    if (t.bits == 64) {
        Image<uint8_t> output(4096, 4096, 256);
        assert(output.data());
        identity_uint8.realize(output);
    } else {
        uint8_t c[4096];
        memset(c, 42, sizeof(c));

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
        Buffer output_buf(UInt(8), &buf);
        identity_uint8.realize(output_buf);
    }

    if (t.bits == 64) {
        assert(!error_occurred);
    } else {
        assert(error_occurred);
    }
    printf("Success!\n");
}
