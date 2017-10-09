#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>

#include "halide_benchmark.h"

#include "pipeline.h"

#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s width height\n", argv[0]);
        return 0;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);

    // Fill the input buffer with random data. This is just a plain
    // old memory buffer.
    uint8_t *memory_to_dma_from = (uint8_t*)malloc(width * height);
    for (int i = 0; i < width * height; i++) {
        memory_to_dma_from[i] = (uint8_t)rand();
    }

    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, height);

    // TODO: We shouldn't need to allocate a host buffer here, but the
    // current implementation of cropping + halide_buffer_copy needs
    // it to work correctly.
    input.allocate();

    // Give the input the buffer we want to DMA from.
    input.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(memory_to_dma_from));
    input.set_device_dirty();

    // In order to actually do a DMA transfer, we need to allocate a
    // DMA engine.
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // We then need to prepare for copying to host. Attempting to copy
    // to host without doing this is an error.
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine);

    Halide::Runtime::Buffer<uint8_t> output(width, height);

    int result = pipeline(input, output);
    if (result != 0) {
        printf("pipeline failed! %d\n", result);
    }

    // Validate that the algorithm did what we expect.
    output.for_each_element([&](int x, int y) {
        uint8_t correct = input(x, y) * 2;
        if (correct != output(x, y)) {
            printf("Mismatch at %d %d: %d != %d\n", x, y, correct, output(x, y));
            abort();
        }
    });

    halide_hexagon_dma_unprepare(nullptr, input);

    // We're done with the DMA engine, release it. This would also be
    // done automatically by device_free.
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(memory_to_dma_from);

    printf("Success!\n");
    return 0;
}
