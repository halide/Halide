// Avoid deprecation warnings
#define HALIDE_ALLOW_DEPRECATED

#include "HalideRuntime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "old_buffer_t.h"

int main(int argc, char **argv) {
    buffer_t in1 = {0}, in2 = {0}, out = {0};
    int scalar_param = 4;

    out.host = (uint8_t *)malloc(60*40*sizeof(int));
    out.extent[0] = 60;
    out.extent[1] = 40;
    out.stride[0] = 1;
    out.stride[1] = 60;

    // Check bounds inference works
    old_buffer_t(&in1, &in2, scalar_param, &out);

    buffer_t correct_in1 = {0, NULL, {62, 44, 0, 0}, {1, 62, 0, 0}, {-1, -1, 0, 0}, 4, false, false, {0}};
    buffer_t correct_in2 = {0, NULL, {60, 40, 0, 0}, {1, 60, 0, 0}, {0, 0, 0, 0}, 4, false, false, {0}};

    if (memcmp(&correct_in1, &in1, sizeof(buffer_t))) {
        printf("Bounds inference gave wrong result for input 1\n");
    }

    if (memcmp(&correct_in2, &in2, sizeof(buffer_t))) {
        printf("Bounds inference gave wrong result for input 2\n");
    }

    // Allocate the inputs
    in1.host = (uint8_t *)malloc(in1.extent[0] * in1.extent[1] * in1.elem_size);
    in2.host = (uint8_t *)malloc(in2.extent[0] * in2.extent[1] * in2.elem_size);

    memset(in1.host, 1, in1.extent[0] * in1.extent[1] * in1.elem_size);
    memset(in2.host, 2, in2.extent[0] * in2.extent[1] * in2.elem_size);

    // Run the pipeline for real
    old_buffer_t(&in1, &in1, scalar_param, &out);

    for (int y = 0; y < out.extent[1]; y++) {
        for (int x = 0; x < out.extent[0]; x++) {
            int result = ((int *)out.host)[y * out.stride[1] + x * out.stride[0]];
            int correct = 0x01010101 + 0x02020202 + scalar_param;
            if (result != correct) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, result, correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
