#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline_nv12_linear_rw_basic.h"
#include "pipeline_nv12_linear_rw_fold.h"
#include "pipeline_nv12_linear_rw_async.h"
#include "pipeline_nv12_linear_rw_split.h"
#include "pipeline_nv12_linear_rw_split_fold.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s width height\n", argv[0]);
        return 0;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);
    const char* str = argv[3];

    // Fill the input buffer with random data. This is just a plain old memory buffer

    const int buf_size = width * height * 1.5;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size ;  i++) {
        data_in[i] =  ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }
    Halide::Runtime::Buffer<uint8_t> input_validation(data_in, width, height, 2);
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, (3*height) / 2);

    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    Halide::Runtime::Buffer<uint8_t> input_y = input.cropped(1, 0, height);    // Luma plane only
    Halide::Runtime::Buffer<uint8_t> input_uv = input.cropped(1, height, height / 2);  // Chroma plane only, with reduced height

    input_uv.embed(2, 0);
    input_uv.raw_buffer()->dim[2].extent = 2;
    input_uv.raw_buffer()->dim[2].stride = 1;

    input_uv.raw_buffer()->dim[0].stride = 2;
    input_uv.raw_buffer()->dim[0].extent = width / 2;

    input_uv.allocate();
    input_y.allocate();

    input_uv.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));

    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_uv, dma_engine, false, halide_hexagon_fmt_NV12_UV);

    input_y.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));

    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_y, dma_engine, false, halide_hexagon_fmt_NV12_Y);

    input_y.set_device_dirty();
    input_uv.set_device_dirty();
    
    Halide::Runtime::Buffer<uint8_t> output(width, (height * 1.5));
    Halide::Runtime::Buffer<uint8_t> output_y = output.cropped(1, 0, height);    // Luma plane only
    Halide::Runtime::Buffer<uint8_t> output_uv = output.cropped(1, height, (height / 2));  // Chroma plane only, with reduced height

    output_uv.embed(2, 0);
    output_uv.raw_buffer()->dimensions = 3;
    output_uv.raw_buffer()->dim[2].extent = 2;
    output_uv.raw_buffer()->dim[2].stride = 1;

    output_uv.raw_buffer()->dim[0].stride = 2;
    output_uv.raw_buffer()->dim[0].extent = width / 2;

    output_y.set_device_dirty();
    output_uv.set_device_dirty();


    output_y.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_out));

    halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output_y, dma_engine, false, halide_hexagon_fmt_NV12_Y);

    output_uv.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_out));

    halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output_uv, dma_engine, false, halide_hexagon_fmt_NV12_UV);

    if (!strcmp(str,"basic")) {
        printf("Basic pipeline\n");
        int result = pipeline_nv12_linear_rw_basic(input_y, input_uv, output_y, output_uv);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
   } else if (!strcmp(str,"fold")) {
        printf("Fold pipeline\n");
        int result = pipeline_nv12_linear_rw_fold(input_y, input_uv, output_y, output_uv);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
   } else if (!strcmp(str,"async")) {
        printf("Async pipeline\n");
        int result = pipeline_nv12_linear_rw_async(input_y, input_uv, output_y, output_uv);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
   } else if (!strcmp(str,"split")) {
        printf("Split pipeline\n");
        int result = pipeline_nv12_linear_rw_split(input_y, input_uv, output_y, output_uv);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
   } else if (!strcmp(str,"split_fold")) {
        printf("Split Fold pipeline\n");
        int result = pipeline_nv12_linear_rw_split_fold(input_y, input_uv, output_y, output_uv);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
   } else {
        printf("Incorrect input Correct options: basic, fold, async, split, split_fold\n");
        free(data_in);
        return -1;
   }

    for (int y = 0; y < 1.5 * height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t correct = data_in[x + y * width] * 2;
            uint8_t result = data_out[x + y * width] ;
            if (correct != result) {
                static int cnt = 0;
                printf("Mismatch at x=%d y=%d : %d != %d\n", x, y, correct, result);
                if (++cnt > 20) abort();
            }
        }
    }

    halide_hexagon_dma_unprepare(nullptr, input_y);
    halide_hexagon_dma_unprepare(nullptr, input_uv);
 
    halide_hexagon_dma_unprepare(nullptr, output_y);
    halide_hexagon_dma_unprepare(nullptr, output_uv);

    // We're done with the DMA engine, release it. This would also be
    // done automatically by device_free.
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);
    free(data_out);

    printf("Success!\n");
    return 0;
}
