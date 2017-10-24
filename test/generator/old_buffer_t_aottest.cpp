// Avoid deprecation warnings
#define HALIDE_ALLOW_DEPRECATED

#include "HalideRuntime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "old_buffer_t.h"
#include <assert.h>

int &get_pixel(buffer_t *buf, int x, int y) {
    return *((int *)(buf->host) +
             (x - buf->min[0]) * buf->stride[0] +
             (y - buf->min[1]) * buf->stride[1]);
}

extern "C" int extern_stage(buffer_t *in2, buffer_t *f, buffer_t *out) {
    bool bounds_query = false;
    if (in2->host == nullptr && in2->dev == 0) {
        in2->extent[0] = out->extent[0];
        in2->min[0] = out->min[0];
        in2->extent[1] = out->extent[1];
        in2->min[1] = out->min[1] + 7;
        bounds_query = true;
    }
    if (f->host == nullptr && f->dev == 0) {
        f->extent[0] = out->extent[0];
        f->min[0] = out->min[0];
        f->extent[1] = out->extent[1];
        f->min[1] = out->min[1];
        bounds_query = true;
    }
    if (bounds_query) {
        return 0;
    }
    halide_copy_to_host_legacy(nullptr, f);
    for (int y = out->min[1]; y < out->min[1] + out->extent[1]; y++) {
        for (int x = out->min[0]; x < out->min[0] + out->extent[0]; x++) {
            get_pixel(out, x, y) = get_pixel(in2, x, y + 7) + get_pixel(f, x, y);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    buffer_t in1 = {0}, in2 = {0}, out = {0};
    int scalar_param = 4;

    out.host = (uint8_t *)malloc(60*40*sizeof(int));
    out.extent[0] = 60;
    out.extent[1] = 40;
    out.stride[0] = 1;
    out.stride[1] = 60;
    out.elem_size = 4;

    // Check bounds inference works
    int err = old_buffer_t_old_buffer_t(&in1, &in2, scalar_param, &out);
    if (err != 0) {
        printf("Pipeline returned non-zero exit status in bounds query mode: %d\n", err);
    }

    buffer_t correct_in1 = {0, NULL, {62, 44, 0, 0}, {1, 62, 0, 0}, {-1, -1, 0, 0}, 4, false, false, {0}};
    buffer_t correct_in2 = {0, NULL, {60, 47, 0, 0}, {1, 60, 0, 0}, {0, 0, 0, 0}, 4, false, false, {0}};

    if (memcmp(&correct_in1, &in1, sizeof(buffer_t))) {
        printf("Bounds inference gave wrong result for input 1\n");
        return -1;
    }

    if (memcmp(&correct_in2, &in2, sizeof(buffer_t))) {
        printf("Bounds inference gave wrong result for input 2\n");
        return -1;
    }

    // Allocate the inputs
    in1.host = (uint8_t *)malloc(in1.extent[0] * in1.extent[1] * in1.elem_size);
    in2.host = (uint8_t *)malloc(in2.extent[0] * in2.extent[1] * in2.elem_size);

    memset(in1.host, 1, in1.extent[0] * in1.extent[1] * in1.elem_size);
    memset(in2.host, 2, in2.extent[0] * in2.extent[1] * in2.elem_size);
    in1.host_dirty = in2.host_dirty = true;

    // Run the pipeline for real
    err = old_buffer_t_old_buffer_t(&in1, &in2, scalar_param, &out);
    if (err != 0) {
        printf("Pipeline returned non-zero exit status: %d\n", err);
    }

    for (int y = 0; y < out.extent[1]; y++) {
        for (int x = 0; x < out.extent[0]; x++) {
            int result = get_pixel(&out, x, y);
            int correct = 0x01010101 * 2 + 0x02020202 * 2 + scalar_param;
            if (result != correct) {
                printf("out(%d, %d) = %x instead of %x\n", x, y, (unsigned)result, (unsigned)correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
