#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#include "pipeline_nv12.h"
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
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA   
    int *data_in_int = reinterpret_cast<int *>(data_in);
    for (int i = 0; i < (buf_size >> 2);  i++) {
        data_in_int[i] = i;
    }
    Halide::Runtime::Buffer<uint8_t> input_validation(data_in, width, height, 2);
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, (3*height) / 2);

    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    Halide::Runtime::Buffer<uint8_t> input_y = input.cropped(1, 0, height);    // Luma plane only
    Halide::Runtime::Buffer<uint8_t> input_uv = input.cropped(1, height, height / 2);  // Chroma plane only, with reduced height

    input_uv.allocate();
    input_y.allocate();

    input_uv.embed(2, 0);
    input_uv.raw_buffer()->dim[2].extent = 2;
    input_uv.raw_buffer()->dim[2].stride = 1;

    input_uv.raw_buffer()->dim[0].stride = 2;
    input_uv.raw_buffer()->dim[0].extent = width / 2;
   

    input_uv.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));

    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_uv, dma_engine, false, eDmaFmt_NV12_UV);

    input_y.device_wrap_native(halide_hexagon_dma_device_interface(),
                             reinterpret_cast<uint64_t>(data_in));

    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_y, dma_engine, false, eDmaFmt_NV12_Y);

    input_y.set_device_dirty();
    input_uv.set_device_dirty();
    
    Halide::Runtime::Buffer<uint8_t> output(width, (height * 1.5));
    Halide::Runtime::Buffer<uint8_t> output_y = output.cropped(1, 0, height);    // Luma plane only
    Halide::Runtime::Buffer<uint8_t> output_c = output.cropped(1, height, (height / 2));  // Chroma plane only, with reduced height

    output_c.embed(2, 0);
    output_c.raw_buffer()->dim[2].extent = 2;
    output_c.raw_buffer()->dim[2].stride = 1;

    output_c.raw_buffer()->dim[0].stride = 2;
    output_c.raw_buffer()->dim[0].extent = width / 2;


    int result = pipeline_nv12(input_y, input_uv, output_y, output_c);
    if (result != 0) {
        printf("pipeline failed! %d\n", result);
    }

    for (int y = 0; y < 1.5 * height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t correct = data_in[x + y * width] * 2;
            if (correct != output(x, y)) {
                static int cnt = 0;
                printf("Mismatch at x=%d y=%d : %d != %d\n", x, y, correct, output(x, y));
                if (++cnt > 20) abort();
            }
        }
    }
    
    halide_hexagon_dma_unprepare(nullptr, input_y);
    halide_hexagon_dma_unprepare(nullptr, input_uv);

    // We're done with the DMA engine, release it. This would also be
    // done automatically by device_free.
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);

    printf("Success!\n");
    return 0;
}
