#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline_p010_linear_ro_basic.h"
#include "pipeline_p010_linear_ro_async.h"
#include "pipeline_p010_linear_ro_fold.h"
#include "pipeline_p010_linear_ro_split.h"
#include "pipeline_p010_linear_ro_split_fold.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    int ret = 0;

    if (argc < 4) {
        printf("Usage: %s width height func {basic, fold, async, split, split_fold} \n", argv[0]);
        return ret;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);
    const char *str = argv[3];

    // Fill the input buffer with random test data. This is just a plain old memory buffer
    const int buf_size = (width * height * 3) / 2;
    uint16_t *data_in = (uint16_t *)malloc(buf_size * sizeof(uint16_t));
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size;  i++) {
        data_in[i] =  ((uint16_t)rand()) >> 1;
    }

    // Setup Halide input buffer with the test buffer
    Halide::Runtime::Buffer<uint16_t> input_validation(data_in, width, height, 2);
    Halide::Runtime::Buffer<uint16_t> input(nullptr, width, (3*height) / 2);
    Halide::Runtime::Buffer<uint16_t> input_y = input.cropped(1, 0, height);            // Luma plane only
    Halide::Runtime::Buffer<uint16_t> input_uv = input.cropped(1, height, height / 2);  // Chroma plane only, with reduced height

    // describe the UV interleaving for 4:2:0 format
    input_uv.embed(2, 0);
    input_uv.raw_buffer()->dim[2].extent = 2;
    input_uv.raw_buffer()->dim[2].stride = 1;
    input_uv.raw_buffer()->dim[0].stride = 2;
    input_uv.raw_buffer()->dim[0].extent = width / 2;

    // DMA_step 1: Assign buffer to DMA interface
    input_y.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));
    input_uv.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));
    input_y.set_device_dirty();
    input_uv.set_device_dirty();

    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_y, dma_engine, false, halide_hexagon_fmt_P010_Y);
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_uv, dma_engine, false, halide_hexagon_fmt_P010_UV);
    
    // Setup Halide output buffer
    Halide::Runtime::Buffer<uint16_t> output(width, (3 * height) / 2);
    Halide::Runtime::Buffer<uint16_t> output_y = output.cropped(1, 0, height);              // Luma plane only
    Halide::Runtime::Buffer<uint16_t> output_uv = output.cropped(1, height, (height / 2));  // Chroma plane only, with reduced height

    // describe the UV interleaving for 4:2:0 format
    output_uv.embed(2, 0);
    output_uv.raw_buffer()->dim[2].extent = 2;
    output_uv.raw_buffer()->dim[2].stride = 1;
    output_uv.raw_buffer()->dim[0].stride = 2;
    output_uv.raw_buffer()->dim[0].extent = width / 2;

    if (!strcmp(str,"basic")) {
        printf("Basic pipeline\n");
        ret = pipeline_p010_linear_ro_basic(input_y, input_uv, output_y, output_uv);
    } else if (!strcmp(str,"fold")) {
        printf("Fold pipeline\n");
        ret = pipeline_p010_linear_ro_fold(input_y, input_uv, output_y, output_uv);
    } else if (!strcmp(str,"async")) {
        printf("Async pipeline\n");
        ret = pipeline_p010_linear_ro_async(input_y, input_uv, output_y, output_uv);
    } else if (!strcmp(str,"split")) {
        printf("Split pipeline\n");
        ret = pipeline_p010_linear_ro_split(input_y, input_uv, output_y, output_uv);
    } else if (!strcmp(str,"split_fold")) {
        printf("Split Fold pipeline\n");
        ret = pipeline_p010_linear_ro_split_fold(input_y, input_uv, output_y, output_uv);
    } else {
        printf("Incorrect input Correct options: basic, fold, async, split, split_fold\n");
        ret = -1;
    }

    if (ret != 0) {
        printf("pipeline failed! %d\n", ret);
    }
    else {
        // verify result by comparing to expected values
        for (int y = 0; y < (3 * height) / 2; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t correct = data_in[x + y * width] * 2;
                if (correct != output(x, y)) {
                    static int cnt = 0;
                    printf("Mismatch at x=%d y=%d : %d != %d\n", x, y, correct, output(x, y));
                    if (++cnt > 20) abort();
                }
            }
        }
        printf("Success!\n");
    }

    // DMA_step 4: Buffer is processed, disassociate buffer from DMA engine
    //             Optional goto DMA_step 0 for processing more buffers
    halide_hexagon_dma_unprepare(nullptr, input_y);
    halide_hexagon_dma_unprepare(nullptr, input_uv);

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);

    return ret;
}
