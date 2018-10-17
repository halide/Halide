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
#include "pipeline_raw_linear_interleaved_rw_basic.h"
#include "pipeline_raw_linear_interleaved_rw_fold.h"
#include "pipeline_raw_linear_interleaved_rw_async.h"
#include "pipeline_raw_linear_interleaved_rw_split.h"
#include "pipeline_raw_linear_interleaved_rw_split_fold.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    int ret = 0;

    if (argc < 4) {
        printf("Usage: %s width height schedule {basic, fold, async, split, split_fold} read_write {ro, rw}\n", argv[0]);
        return ret;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);
    const char *schedule = argv[3];
    const char *read_write = argv[4];

    // Fill the input buffer with random test data. This is just a plain old memory buffer
    const int buf_size = width * height * 4;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size;  i++) {
        data_in[i] = ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }

    // Setup Halide input buffer with the test buffer
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, height, 4);

    input = input.make_interleaved(width, height, 4);
 
    // Setup Halide output buffer
    Halide::Runtime::Buffer<uint8_t> output(width, height, 4);

    output = output.make_interleaved(width, height, 4);
 
    // DMA_step 1: Assign buffer to DMA interface
    input.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_in));
    input.set_device_dirty();

    if (!strcmp(read_write, "rw")) {
        output.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_out));
        output.set_device_dirty();
    }
    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read) and device (DMA write)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, halide_hexagon_fmt_RawData);

    if (!strcmp(read_write, "rw")) {
        halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output, dma_engine, false, halide_hexagon_fmt_RawData);
    }

    if (!strcmp(schedule,"basic")) {
        printf("Basic pipeline\n");
        if (!strcmp(read_write, "rw")) {
            ret = pipeline_raw_linear_interleaved_rw_basic(input, output);
        } else {
            ret = pipeline_raw_linear_interleaved_ro_basic(input, output);
        }
    } else if (!strcmp(schedule,"fold")) {
        printf("Fold pipeline\n");
        if (!strcmp(read_write, "rw")) {
            ret = pipeline_raw_linear_interleaved_rw_fold(input, output);
        } else {
            ret = pipeline_raw_linear_interleaved_ro_fold(input, output);
        }
    } else if (!strcmp(schedule,"async")) {
        printf("Async pipeline\n");
        if (!strcmp(read_write, "rw")) {
            ret = pipeline_raw_linear_interleaved_rw_async(input, output);
        } else {
            ret = pipeline_raw_linear_interleaved_ro_async(input, output);
        }
    } else if (!strcmp(schedule,"split")) {
        printf("Split pipeline\n");
        if (!strcmp(read_write, "rw")) {
            ret = pipeline_raw_linear_interleaved_rw_split(input, output);
        } else {
            ret = pipeline_raw_linear_interleaved_ro_split(input, output);
        }
    } else if (!strcmp(schedule,"split_fold")) {
        printf("Split Fold pipeline\n");
        if (!strcmp(read_write, "rw")) {
            ret = pipeline_raw_linear_interleaved_rw_split_fold(input, output);
        } else {
            ret = pipeline_raw_linear_interleaved_ro_split_fold(input, output);
        }
    } else {
        printf("Incorrect input Correct options: basic, fold, async, split, split_fold\n");
        ret = -1;
    }

    if (ret != 0) {
        printf("pipeline failed! %d\n", ret);
    }
    else {
        // verify result by comparing to expected values
        int error_count = 0;
        for (int y=0; y < height; y++) {
            for (int x=0; x < width; x++) {
                for (int z=0; z < 4; z++) {      
                    uint8_t correct = data_in[x*4 + z + y*width*4] * 2;
                    uint8_t result = (!strcmp(read_write, "rw")) ? data_out[x*4 + z + y*width*4] : output(x, y, z);
                    if (correct != result) {
                        printf("Mismatch at x=%d y=%d z=%d: %d != %d\n", x, y, z, correct, result);
                        if (++error_count > 20) abort();
                    }
                }
            }
        }
        printf("Success!\n");
    }

    // DMA_step 4: Buffer is processed, disassociate buffer from DMA engine
    //             Optional goto DMA_step 0 for processing more buffers
    halide_hexagon_dma_unprepare(nullptr, input);
 
    if (!strcmp(read_write, "rw")) {
        halide_hexagon_dma_unprepare(nullptr, output);
    }

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);
    free(data_out);

    return ret;
}
