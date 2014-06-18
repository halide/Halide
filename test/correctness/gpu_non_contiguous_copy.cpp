#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Image<int> full = lambda(x, y, x * y).realize(800, 600);

    buffer_t cropped = {0};
    cropped.host = (uint8_t *)(&full(40, 80));
    cropped.host_dirty = true;
    cropped.elem_size = 4;
    cropped.min[0] = 40;
    cropped.min[1] = 80;
    cropped.extent[0] = 128;
    cropped.extent[1] = 96;
    cropped.stride[0] = full.stride(0);
    cropped.stride[1] = full.stride(1);
    Buffer out(Int(32), &cropped);

    Func f;

    f(x, y) = x*y + 1;
    f.gpu_tile(x, y, 16, 16);
    f.realize(out);

    // Put something secret outside of the region that the func is
    // supposed to write to.
    full(500, 80) = 1234567;

    // Copy back the output.
    out.copy_to_host();

    if (full(500, 80) != 1234567) {
        printf("Output value outside of the range evaluated was clobbered by copy-back from gpu!\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
