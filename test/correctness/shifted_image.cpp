#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    halide_buffer_t buf = {0};
    halide_dimension_t shape[] = {{100, 10, 1},
                                  {300, 10, 10},
                                  {500, 10, 100},
                                  {400, 10, 1000}};
    buf.host = (uint8_t *)malloc(10000 * sizeof(int));
    buf.dim = shape;
    buf.dimensions = 4;
    buf.type = Int(32);
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
