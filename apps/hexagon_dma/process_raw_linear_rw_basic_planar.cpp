#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "pipeline_raw_linear_rw_basic_planar.h"
#include "pipeline_raw_linear_rw_fold_planar.h"
#include "pipeline_raw_linear_rw_async_planar.h"
#include "pipeline_raw_linear_rw_split_planar.h"
#include "pipeline_raw_linear_rw_split_fold_planar.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s width height\n", argv[0]);
        return 0;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);
    const char *str = argv[3];

    // Fill the input buffer with random data. This is just a plain old memory buffer

    const int buf_size = width * height * 4;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size ;  i++) {
        data_in[i] =  ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }
    Halide::Runtime::Buffer<uint8_t> input_validation(data_in, width, height, 4);
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, height, 4);

    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    input.allocate();

    input.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));

    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, halide_hexagon_fmt_RawData);

    input.set_device_dirty();
    
    Halide::Runtime::Buffer<uint8_t> output(width, height, 4);
    output.set_device_dirty();
    output.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_out));
    halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output, dma_engine, false, halide_hexagon_fmt_RawData);

    if (!strcmp(str,"basic")) {
        int result = pipeline_raw_linear_rw_basic_planar(input, output);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    } else if (!strcmp(str,"fold")) {
        int result = pipeline_raw_linear_rw_fold_planar(input, output);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    } else if (!strcmp(str,"async")) {
        int result = pipeline_raw_linear_rw_async_planar(input, output);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    }  else if (!strcmp(str,"split")) {
        int result = pipeline_raw_linear_rw_split_planar(input, output);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    } else if (!strcmp(str,"split_fold")) {
        int result = pipeline_raw_linear_rw_split_fold_planar(input, output);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    } else {
        printf("Incorrect input Correct options: basic, fold, async, split, split_fold\n");
        free(data_in);
        return -1;
    }

    for (int z=0; z < 4; z++) {
        for (int y=0; y < height; y++) {
            for (int x=0; x < width; x++) {
                uint8_t correct = data_in[x + y*width + z * width * height] * 2;
                if (correct != data_out[x + y*width + z * width * height] ) {
                    static int cnt = 0;
                    printf("Mismatch at x=%d y=%d z=%d: %d != %d\n", x, y, z, correct, data_out[x + y*width + z * width * height]);
                    if (++cnt > 20) abort();
                }
            }
        }
    }

    halide_hexagon_dma_unprepare(nullptr, input);
    halide_hexagon_dma_unprepare(nullptr, output);

    // We're done with the DMA engine, release it. This would also be
    // done automatically by device_free.
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);
    free(data_out);

    printf("Success!\n");
    return 0;
}
