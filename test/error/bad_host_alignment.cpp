#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

IRPrinter irp(std::cerr);
int main(int argc, char **argv) {
    Func f;
    Var x, y;
    int arr[11][10];
    uint8_t *ptr  = reinterpret_cast<uint8_t*>(arr);
    ptr += 1;
    buffer_t buf;
    buf.host = ptr;
    buf.extent[0] = 10;
    buf.extent[1] = 10;
    buf.stride[0] = 1;
    buf.stride[1] = 10;
    buf.elem_size = 1;
    buf.min[0] = 0;
    buf.min[1] = 0;
    Buffer param_buf(UInt(8), &buf);
    ImageParam in(UInt(8), 2);

    in.set_host_alignment(512);
    f(x, y) = in(x, y);
    f.compute_root();

    in.set(param_buf);
    Image<uint8_t> result = f.realize(10, 10);

    printf("I should not have reached here\n");

    return 0;
}
