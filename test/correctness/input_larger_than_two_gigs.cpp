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

    Buffer param_buf(UInt(8), &buf);
    ImageParam input(UInt(8), 3);
    input.set(param_buf);

    RDom r(0, input.extent(0), 0, input.extent(1), 0, input.extent(2));
    Var x;
    Func grand_total;
    grand_total() = cast<uint64_t>(sum(cast<uint64_t>(input(r.x, r.y, r.z))));
    grand_total.set_error_handler(&halide_error);

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
