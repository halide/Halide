#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    halide_dimension_t shape[] = {{100, 10, 1},
                                  {300, 10, 10},
                                  {500, 10, 100},
                                  {400, 10, 1000}};
    Buffer<int> buf(nullptr, 4, shape);
    buf.allocate();

    buf.data()[0] = 17;
    if (buf(100, 300, 500, 400) != 17) {
        printf("Image indexing into buffers with non-zero mins is broken\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
