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

    halide_buffer_t buf = {0};
    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    buf.dim = shape;
    buf.dimensions = 3;
    buf.type = UInt(8);
    buf.host = c;

    Buffer param_buf(&buf);
    ImageParam input(UInt(8), 3);
    input.set(param_buf);

    RDom r(input);
    Func grand_total;
    grand_total() = cast<uint8_t>(sum(input(r.x, r.y, r.z)));
    grand_total.set_error_handler(&halide_error);

    Image<uint8_t> result = grand_total.realize();

    assert(error_occurred);
    printf("Success!\n");
}
