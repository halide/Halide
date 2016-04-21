#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

IRPrinter irp(std::cerr);
int main(int argc, char **argv) {
    Func f;
    Var x, y;
    int arr[11][10];
    uint8_t *ptr = reinterpret_cast<uint8_t*>(arr);
    ptr += 1;
    halide_nd_buffer_t<1> buf;
    buf.host = ptr;
    buf.dim[0] = {0, 10, 1};
    buf.type = halide_type_of<uint8_t>();
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
