#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"
#include "../../src/runtime/mini_hexagon_dma.h"

void halide_print(void *user_context, const char *msg) {
    printf("halide_print %s\n", msg);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s width height\n", argv[0]);
        return 0;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);

    // Fill the input buffer with random test data. This is just a plain old memory buffer
    const int buf_size = width * height;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    for (int i = 0; i < buf_size;  i++) {
        data_in[i] = ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }

    // Setup Halide input buffer with the test buffer
    Halide::Runtime::Buffer<uint8_t> input_validation(data_in, width, height);
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, height);

    // DMA_step 1: Assign buffer to DMA interface
    input.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));
    input.set_device_dirty();

    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, halide_hexagon_fmt_RawData);

    // Setup Halide output buffer
    Halide::Runtime::Buffer<uint8_t> output(width, height);
    output.set_device_dirty();
    output.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_out));
    halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output, dma_engine, false, halide_hexagon_fmt_RawData);


    int result = pipeline(input, output);
    if (result != 0) {
        printf("pipeline failed! %d\n", result);
    }

    // verify result by comparing to expected values
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t correct = data_in[x + y*width ] * 2;
            if (correct != data_out[x + y*width]) {
                static int cnt = 0;
                printf("Mismatch at x=%d y=%d : %d != %d\n", x, y, correct, output(x, y));
                if (++cnt > 20) abort();
            }
        }
    }

    // DMA_step 4: Buffer is processed, disassociate buffer from DMA engine
    //             Optional goto DMA_step 0 for processing more buffers
    halide_hexagon_dma_unprepare(nullptr, input);
    halide_hexagon_dma_unprepare(nullptr, output);

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);
    free(data_out);

    printf("Success!\n");
    return 0;
}
