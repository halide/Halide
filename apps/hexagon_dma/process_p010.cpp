#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline_p010.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"
#include "../../src/runtime/mini_hexagon_dma.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s width height\n", argv[0]);
        return 0;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);

    // Fill the input buffer with random data. This is just a plain old memory buffer
    const int buf_size = width * height * 1.5;
    uint16_t *memory_to_dma_from = (uint16_t *)malloc(buf_size * sizeof(uint16_t));
    for (int i = 0; i < buf_size;  i++) {
        memory_to_dma_from[i] = ((uint16_t)rand()) >> 1;
    }

    Halide::Runtime::Buffer<uint16_t> input_validation(memory_to_dma_from, width, height, 2);
    Halide::Runtime::Buffer<uint16_t> input(nullptr, width, height, 2);

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
    // The Last parameter 0 indicate DMA Read
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, eDmaFmt_P010);

    Halide::Runtime::Buffer<uint16_t> output(width, height, 2);
    Halide::Runtime::Buffer<uint16_t> output_y = output.cropped(2, 0, 1);    // Luma plane only
    Halide::Runtime::Buffer<uint16_t> output_c = output.cropped(2, 1, 1).cropped(1, 0, (height/2));  // Chroma plane only, with reduced height

    int result = pipeline_p010(input, output_y, output_c);
    if (result != 0) {
        printf("pipeline failed! %d\n", result);
    }

    output.copy_to_host();
    const int plane_start = 0;
    const int plane_end = 2;
    printf("plane start=%d end=%d\n", plane_start, plane_end);
    for (int c = plane_start; c < plane_end; c++) {
        int height_c = (c==1) ? height/2 : height;
        for (int y = 0; y < height_c; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t correct = memory_to_dma_from[x + y*width + c*width*height] * 2;
                if (correct != output(x, y, c)) {
                    static int cnt = 0;
                    printf("Mismatch at x=%d y=%d c=%d : %d != %d\n", x, y, c, correct, output(x, y, c));
                    if (++cnt > 20) abort();
                }
            }
        }
    }

    halide_hexagon_dma_unprepare(nullptr, input);

    // We're done with the DMA engine, release it. This would also be
    // done automatically by device_free.
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(memory_to_dma_from);

    printf("Success!\n");
    return 0;
}
