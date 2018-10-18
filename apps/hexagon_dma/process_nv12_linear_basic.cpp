#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "halide_benchmark.h"
#ifdef SCHEDULE_INCLUDE_RO
#include "pipeline_nv12_linear_ro_basic.h"
#include "pipeline_nv12_linear_ro_async.h"
#include "pipeline_nv12_linear_ro_fold.h"
#include "pipeline_nv12_linear_ro_split.h"
#include "pipeline_nv12_linear_ro_split_fold.h"
#endif
#include "pipeline_nv12_linear_rw_basic.h"
#include "pipeline_nv12_linear_rw_fold.h"
#include "pipeline_nv12_linear_rw_async.h"
#include "pipeline_nv12_linear_rw_split.h"
#include "pipeline_nv12_linear_rw_split_fold.h"
#include "HalideRuntimeHexagonDma.h"
#include "HalideBuffer.h"


enum {
    SCHEDULE_BASIC,
    SCHEDULE_FOLD,
    SCHEDULE_ASYNC,
    SCHEDULE_SPLIT,
    SCHEDULE_SPLIT_FOLD,
    SCHEDULE_MAX
};

enum {
    DIRECTION_RW,
    DIRECTION_RO,
    DIRECTION_MAX
};

typedef struct {
    const char *schedule_name;
    int (*schedule_call)(struct halide_buffer_t *in_y, struct halide_buffer_t *in_uv, struct halide_buffer_t *out_y, struct halide_buffer_t *out_uv);
} ScheduleList;

#define _SCHEDULE_STR(s) #s
#define _SCHEDULE_NAME(data, direction, schedule)       pipeline_##data##_##direction##_##schedule
#define _SCHEDULE_PAIR(data, direction, schedule)       {_SCHEDULE_STR(scheduled-pipeline(data, direction, schedule)), _SCHEDULE_NAME(data, direction, schedule)}
#define _SCHEDULE_DUMMY_PAIR                            {NULL, NULL}
#define SCHEDULE_FUNCTION_RW(schedule)                  _SCHEDULE_PAIR(nv12_linear, rw, schedule)

#ifdef SCHEDULE_INCLUDE_RO
#define SCHEDULE_FUNCTION_RO(schedule)                  _SCHEDULE_PAIR(nv12_linear, ro, schedule)
#else
#define SCHEDULE_FUNCTION_RO(schedule)                  _SCHEDULE_DUMMY_PAIR
#endif

static ScheduleList schedule_list[DIRECTION_MAX][SCHEDULE_MAX] = {{
    SCHEDULE_FUNCTION_RW(basic),
    SCHEDULE_FUNCTION_RW(fold),
    SCHEDULE_FUNCTION_RW(async),
    SCHEDULE_FUNCTION_RW(split),
    SCHEDULE_FUNCTION_RW(fold)
    },{
    SCHEDULE_FUNCTION_RO(basic),
    SCHEDULE_FUNCTION_RO(fold),
    SCHEDULE_FUNCTION_RO(async),
    SCHEDULE_FUNCTION_RO(split),
    SCHEDULE_FUNCTION_RO(fold)
}};

int main(int argc, char **argv) {
    int ret = 0;

    if (argc < 4) {
        printf("Usage: %s width height schedule {basic, fold, async, split, split_fold} dma_direction {ro, rw}\n", argv[0]);
        return ret;
    }

    const int width = atoi(argv[1]);
    const int height = atoi(argv[2]);
    const char *schedule = argv[3];
    const char *dma_direction = argv[4];

    // Fill the input buffer with random test data. This is just a plain old memory buffer
    const int buf_size = (width * height * 3) / 2;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size ;  i++) {
        data_in[i] =  ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }

    // Setup Halide input buffer with the test buffer
    Halide::Runtime::Buffer<uint8_t> input_validation(data_in, width, height, 2);
    Halide::Runtime::Buffer<uint8_t> input(nullptr, width, (3 * height) / 2);
    Halide::Runtime::Buffer<uint8_t> input_y = input.cropped(1, 0, height);             // Luma plane only
    Halide::Runtime::Buffer<uint8_t> input_uv = input.cropped(1, height, height / 2);   // Chroma plane only, with reduced height

    // describe the UV interleaving for 4:2:0 format
    input_uv.embed(2, 0);
    input_uv.raw_buffer()->dim[2].extent = 2;
    input_uv.raw_buffer()->dim[2].stride = 1;
    input_uv.raw_buffer()->dim[0].stride = 2;
    input_uv.raw_buffer()->dim[0].extent = width / 2;

    // Setup Halide output buffer
    Halide::Runtime::Buffer<uint8_t> output(width, (3 * height) / 2);
    Halide::Runtime::Buffer<uint8_t> output_y = output.cropped(1, 0, height);               // Luma plane only
    Halide::Runtime::Buffer<uint8_t> output_uv = output.cropped(1, height, (height / 2));   // Chroma plane only, with reduced height

    // describe the UV interleaving for 4:2:0 format
    output_uv.embed(2, 0);
    output_uv.raw_buffer()->dimensions = 3;
    output_uv.raw_buffer()->dim[2].extent = 2;
    output_uv.raw_buffer()->dim[2].stride = 1;
    output_uv.raw_buffer()->dim[0].stride = 2;
    output_uv.raw_buffer()->dim[0].extent = width / 2;

    // DMA_step 1: Assign buffer to DMA interface
    input_y.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_in));
    input_uv.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_in));
    input_y.set_device_dirty();
    input_uv.set_device_dirty();

    if (!strcmp(dma_direction, "rw")) {
        output_y.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_out));
        output_uv.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_out));
        output_y.set_device_dirty();
        output_uv.set_device_dirty();
    }

    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read) and device (DMA write)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_y, dma_engine, false, halide_hexagon_fmt_NV12_Y);
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input_uv, dma_engine, false, halide_hexagon_fmt_NV12_UV);
    if (!strcmp(dma_direction, "rw")) {
        halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output_y, dma_engine, false, halide_hexagon_fmt_NV12_Y);
        halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output_uv, dma_engine, false, halide_hexagon_fmt_NV12_UV);
    }

    int my_direction = (!strcmp(dma_direction, "rw")) ? DIRECTION_RW : DIRECTION_RO;
    int my_schedule = SCHEDULE_MAX;
    if (!strcmp(schedule,"basic")) {
        my_schedule = SCHEDULE_BASIC;
    } else if (!strcmp(schedule,"fold")) {
        my_schedule = SCHEDULE_FOLD;
    } else if (!strcmp(schedule,"async")) {
        my_schedule = SCHEDULE_ASYNC;
    } else if (!strcmp(schedule,"split")) {
        my_schedule = SCHEDULE_SPLIT;
    } else if (!strcmp(schedule,"split_fold")) {
        my_schedule = SCHEDULE_SPLIT_FOLD;
    }
    if (my_schedule < SCHEDULE_MAX) {
        if (schedule_list[my_direction][my_schedule].schedule_name != NULL) {
            printf("%s\n", schedule_list[my_direction][my_schedule].schedule_name);
            ret = (*schedule_list[my_direction][my_schedule].schedule_call)(input_y, input_uv, output_y, output_uv);
        } else {
            printf("Schedule pipeline test not built-in (%s, %s)\n", dma_direction, schedule);
            ret = -2;
        }
    } else {
        printf("Incorrect input Correct schedule: basic, fold, async, split, split_fold\n");
        ret = -1;
    }

    if (ret != 0) {
        printf("pipeline failed! %d\n", ret);
    } else {
        // verify result by comparing to expected values
        int error_count = 0;
        for (int y = 0; y < (3 * height) / 2; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t correct = data_in[x + y * width] * 2;
                uint8_t result = (!strcmp(dma_direction, "rw")) ? data_out[x + y * width] : output(x, y);
                if (correct != result) {
                    printf("Mismatch at x=%d y=%d : %d != %d\n", x, y, correct, result);
                    if (++error_count > 20) abort();
                }
            }
        }
        printf("Success!\n");
    }

    // DMA_step 4: Buffer is processed, disassociate buffer from DMA engine
    //             Optional goto DMA_step 0 for processing more buffers
    halide_hexagon_dma_unprepare(nullptr, input_y);
    halide_hexagon_dma_unprepare(nullptr, input_uv);
 
    if (!strcmp(dma_direction, "rw")) {
        halide_hexagon_dma_unprepare(nullptr, output_y);
        halide_hexagon_dma_unprepare(nullptr, output_uv);
    }

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    free(data_in);
    free(data_out);

    return ret;
}
