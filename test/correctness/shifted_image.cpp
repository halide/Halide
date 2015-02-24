#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    buffer_t buf = {0};
    buf.host = (uint8_t *)malloc(10000 * sizeof(int));
    buf.min[0] = 100;
    buf.min[1] = 300;
    buf.min[2] = 500;
    buf.min[3] = 400;
    buf.extent[0] = 10;
    buf.extent[1] = 10;
    buf.extent[2] = 10;
    buf.extent[3] = 10;
    buf.stride[0] = 1;
    buf.stride[1] = 10;
    buf.stride[2] = 100;
    buf.stride[3] = 1000;
    buf.elem_size = 4;

    Image<int> im(&buf);

    ((int *)buf.host)[0] = 17;
    buf.host[0] = 17;
    if (im(100, 300, 500, 400) != 17) {
        printf("Image indexing into buffers with non-zero mins is broken\n");
        return -1;
    }

    free(buf.host);

    return 0;
}
