#include <stdio.h>
#include <stdlib.h>

#include "nested_externs_root.h"

int main(int argc, char **argv) {
    halide_buffer_t buf = {0};
    halide_dimension_t shape[] = {{0, 100, 3},
                                  {0, 200, 100*3},
                                  {0, 3, 1}};
    buf.host = (uint8_t *)malloc(4 * 100 * 200 * 3);
    buf.dim = shape;
    buf.dimensions = 3;
    buf.type = halide_type_of<float>();
    nested_externs_root(38.5f, &buf);

    float *ptr = (float *)buf.host;
    for (int y = 0; y < buf.dim[1].extent; y++) {
        for (int x = 0; x < buf.dim[0].extent; x++) {
            for (int c = 0; c < buf.dim[2].extent; c++) {
                float correct = 158.0f;
                float actual = ptr[x*buf.dim[0].stride + y*buf.dim[1].stride + c*buf.dim[2].stride];
                if (actual != correct) {
                    printf("result(%d, %d, %d) = %f instead of %f\n",
                           x, y, c, actual, correct);
                    return -1;
                }
            }
        }
    }

    free(buf.host);

    printf("Success!\n");
    return 0;
}
