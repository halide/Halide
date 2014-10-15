#include <stdio.h>
#include <stdlib.h>

#include "nested_externs_root.h"

int main(int argc, char **argv) {
    buffer_t buf = {0};
    buf.extent[0] = 100;
    buf.extent[1] = 200;
    buf.extent[2] = 3;
    buf.stride[0] = 3;
    buf.stride[1] = 100*3;
    buf.stride[2] = 1;
    buf.elem_size = 4;
    buf.host = (uint8_t *)malloc(4 * 100 * 200 * 3);

    nested_externs_root(38.5f, &buf);

    float *ptr = (float *)buf.host;
    for (int y = 0; y < buf.extent[1]; y++) {
        for (int x = 0; x < buf.extent[0]; x++) {
            for (int c = 0; c < buf.extent[2]; c++) {
                float correct = 158.0f;
                float actual = ptr[x*buf.stride[0] + y*buf.stride[1] + c*buf.stride[2]];
                if (actual != correct) {
                    printf("result(%d, %d, %d) = %f instead of %f\n",
                           x, y, c, actual, correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");

    free(buf.host);

    return 0;
}
