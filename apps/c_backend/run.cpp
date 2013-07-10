#include "pipeline_native.h"
#include "pipeline_c.h"
#include "../support/static_image.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    Image<uint16_t> in(1432, 324);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint16_t)rand();
        }
    }

    Image<uint16_t> out_native(423, 633);
    Image<uint16_t> out_c(423, 633);

    pipeline_native(in, out_native);
    pipeline_c(in, out_c);

    for (int y = 0; y < out_native.height(); y++) {
        for (int x = 0; x < out_native.width(); x++) {
            if (out_native(x, y) != out_c(x, y)) {
                printf("out_native(%d, %d) = %d, but out_c(%d, %d) = %d\n",
                       x, y, out_native(x, y),
                       x, y, out_c(x, y));
            }
        }
    }

    printf("Success!\n");
    return 0;
}
