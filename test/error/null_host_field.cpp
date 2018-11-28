#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Func f;
    Var x, y;
    ImageParam in(UInt(8), 2);

    // Give the input a device field (to get past bounds query checks)
    // but no host field. If not for the assert failure, we'd segfault
    // (which isn't accepted as correct behavior from the testing
    // infrastructure).
    Buffer<uint8_t> param_buf(10, 10);
    param_buf.raw_buffer()->device = 3;
    param_buf.raw_buffer()->device_interface = (halide_device_interface_t *)(3);
    param_buf.raw_buffer()->host = nullptr;

    f(x, y) = in(x, y);
    f.compute_root();

    in.set(param_buf);
    Buffer<uint8_t> result = f.realize(10, 10);

    // Avoid a freak-out in the destructor of param_buf.
    param_buf.raw_buffer()->device = 0;
    param_buf.raw_buffer()->device_interface = 0;

    printf("I should not have reached here\n");

    return 0;
}
