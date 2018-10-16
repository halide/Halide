#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline_raw_linear_interleaved_ro_basic.h"
#include "pipeline_raw_linear_interleaved_ro_fold.h"
#include "pipeline_raw_linear_interleaved_ro_async.h"
#include "pipeline_raw_linear_interleaved_ro_split.h"
#include "pipeline_raw_linear_interleaved_ro_split_fold.h"
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
    const int buf_size = width * height * 4;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size;  i++) {
        data_in[i] = ((uint8_t)rand()) >> 1;
    }

    // Setup Halide input buffer with the test buffer
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, height, 4);

    input = input.make_interleaved(width, height, 4);
 
    // DMA_step 1: Assign buffer to DMA interface
    input.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_in));
    input.set_device_dirty();

    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, halide_hexagon_fmt_RawData);

    // Setup Halide output buffer
    Halide::Runtime::Buffer<uint8_t> output(width, height, 4);

    output = output.make_interleaved(width, height, 4);

    if (!strcmp(str,"basic")) {
        printf("Basic pipeline\n");
        ret = pipeline_raw_linear_interleaved_ro_basic(input, output);
    } else if (!strcmp(str,"fold")) {
        printf("Fold pipeline\n");
        ret = pipeline_raw_linear_interleaved_ro_fold(input, output);
    } else if (!strcmp(str,"async")) {
        printf("Async pipeline\n");
        ret = pipeline_raw_linear_interleaved_ro_async(input, output);
    } else if (!strcmp(str,"split")) {
        printf("Split pipeline\n");
        ret = pipeline_raw_linear_interleaved_ro_split(input, output);
    } else if (!strcmp(str,"split_fold")) {
        printf("Split Fold pipeline\n");
        ret = pipeline_raw_linear_interleaved_ro_split_fold(input, output);
    } else {
        printf("Incorrect input Correct options: basic, fold, async, split, split_fold\n");
        ret = -1;
    }

    if (ret != 0) {
        printf("pipeline failed! %d\n", ret);
    }
    else {
        // verify result by comparing to expected values
        for (int y=0; y < height; y++) {
            for (int x=0; x < width; x++) {
                for (int z=0; z < 4; z++) {      
                    uint8_t correct = data_in[x*4 + z + y*width*4] * 2;
                    if (correct != output(x, y, z)) {
                        static int cnt = 0;
                        printf("Mismatch at x=%d y=%d z=%d: %d != %d\n", x, y, z, correct, output(x, y, z));
                        if (++cnt > 20) abort();
                    }
                }
            }
        }
        printf("Success!\n");
    }

    // DMA_step 4: Buffer is processed, disassociate buffer from DMA engine
    //             Optional goto DMA_step 0 for processing more buffers
    halide_hexagon_dma_unprepare(nullptr, input);

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);

    return ret;
}
