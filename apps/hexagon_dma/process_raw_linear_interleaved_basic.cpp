#include "halide_benchmark.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef SCHEDULE_ALL
#include "pipeline_raw_linear_interleaved_ro_async.h"
#include "pipeline_raw_linear_interleaved_ro_basic.h"
#include "pipeline_raw_linear_interleaved_ro_fold.h"
#include "pipeline_raw_linear_interleaved_ro_split.h"
#include "pipeline_raw_linear_interleaved_ro_split_async.h"

#include "pipeline_raw_linear_interleaved_rw_basic.h"
#include "pipeline_raw_linear_interleaved_rw_fold.h"
#endif
#include "HalideBuffer.h"
#include "HalideRuntimeHexagonDma.h"
#include "pipeline_raw_linear_interleaved_rw_async.h"
#include "pipeline_raw_linear_interleaved_rw_split.h"
#include "pipeline_raw_linear_interleaved_rw_split_async.h"

enum {
    SCHEDULE_BASIC,
    SCHEDULE_FOLD,
    SCHEDULE_ASYNC,
    SCHEDULE_SPLIT,
    SCHEDULE_SPLIT_ASYNC,
    SCHEDULE_MAX
};

enum {
    DIRECTION_RW,
    DIRECTION_RO,
    DIRECTION_MAX
};

typedef struct {
    const char *schedule_name;
    int (*schedule_call)(struct halide_buffer_t *in, struct halide_buffer_t *out);
} ScheduleList;

#define _SCHEDULE_STR(s) #s
#define _SCHEDULE_NAME(data, direction, schedule) pipeline_##data##_##direction##_##schedule
#define _SCHEDULE_PAIR(data, direction, schedule) \
    { _SCHEDULE_STR(scheduled - pipeline(data, direction, schedule)), _SCHEDULE_NAME(data, direction, schedule) }
#define _SCHEDULE_DUMMY_PAIR \
    { NULL, NULL }
#define SCHEDULE_FUNCTION_RW(schedule) _SCHEDULE_PAIR(raw_linear_interleaved, rw, schedule)

#ifdef SCHEDULE_ALL
#define SCHEDULE_FUNCTION_RO(schedule) _SCHEDULE_PAIR(raw_linear_interleaved, ro, schedule)
#else
#define SCHEDULE_FUNCTION_RO(schedule) _SCHEDULE_DUMMY_PAIR
#endif

static ScheduleList schedule_list[DIRECTION_MAX][SCHEDULE_MAX] = {{
#ifdef SCHEDULE_ALL
                                                                      SCHEDULE_FUNCTION_RW(basic),
                                                                      SCHEDULE_FUNCTION_RW(fold),
#else
                                                                      SCHEDULE_FUNCTION_RO(basic),  // dummy
                                                                      SCHEDULE_FUNCTION_RO(fold),   // dummy
#endif
                                                                      SCHEDULE_FUNCTION_RW(async),
                                                                      SCHEDULE_FUNCTION_RW(split),
                                                                      SCHEDULE_FUNCTION_RW(split_async)},
                                                                  {SCHEDULE_FUNCTION_RO(basic),
                                                                   SCHEDULE_FUNCTION_RO(fold),
                                                                   SCHEDULE_FUNCTION_RO(async),
                                                                   SCHEDULE_FUNCTION_RO(split),
                                                                   SCHEDULE_FUNCTION_RO(split_async)}};

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
    const int buf_size = width * height * 4;
    uint8_t *data_in = (uint8_t *)malloc(buf_size);
    uint8_t *data_out = (uint8_t *)malloc(buf_size);
    // Creating the Input Data so that we can catch if there are any Errors in DMA
    for (int i = 0; i < buf_size; i++) {
        data_in[i] = ((uint8_t)rand()) >> 1;
        data_out[i] = 0;
    }

    // Setup Halide input buffer with the test buffer
    auto input = Halide::Runtime::Buffer<uint8_t, 3>::make_interleaved(width, height, 4);

    // Setup Halide output buffer
    auto output = Halide::Runtime::Buffer<uint8_t, 3>::make_interleaved(width, height, 4);

    // DMA_step 1: Assign buffer to DMA interface
    input.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_in));
    input.set_device_dirty();

    if (!strcmp(dma_direction, "rw")) {
        output.device_wrap_native(halide_hexagon_dma_device_interface(), reinterpret_cast<uint64_t>(data_out));
        output.set_device_dirty();
    }
    // DMA_step 2: Allocate a DMA engine
    void *dma_engine = nullptr;
    void *dma_engine_write = nullptr;
    halide_hexagon_dma_allocate_engine(nullptr, &dma_engine);

    if ((!strcmp(schedule, "async") || !strcmp(schedule, "split_async")) && !strcmp(dma_direction, "rw")) {
        printf("A separate engine for DMA write\n");
        halide_hexagon_dma_allocate_engine(nullptr, &dma_engine_write);
    }
    // DMA_step 3: Associate buffer to DMA engine, and prepare for copying to host (DMA read) and device (DMA write)
    halide_hexagon_dma_prepare_for_copy_to_host(nullptr, input, dma_engine, false, halide_hexagon_fmt_RawData);

    if (!strcmp(dma_direction, "rw")) {
        if (!strcmp(schedule, "async") || !strcmp(schedule, "split_async")) {
            printf("Use separate engine for DMA output\n");
            halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output, dma_engine_write, false, halide_hexagon_fmt_RawData);
        } else {
            halide_hexagon_dma_prepare_for_copy_to_device(nullptr, output, dma_engine, false, halide_hexagon_fmt_RawData);
        }
    }

    int my_direction = (!strcmp(dma_direction, "rw")) ? DIRECTION_RW : DIRECTION_RO;
    int my_schedule = SCHEDULE_MAX;
    if (!strcmp(schedule, "basic")) {
        my_schedule = SCHEDULE_BASIC;
    } else if (!strcmp(schedule, "fold")) {
        my_schedule = SCHEDULE_FOLD;
    } else if (!strcmp(schedule, "async")) {
        my_schedule = SCHEDULE_ASYNC;
    } else if (!strcmp(schedule, "split")) {
        my_schedule = SCHEDULE_SPLIT;
    } else if (!strcmp(schedule, "split_async")) {
        my_schedule = SCHEDULE_SPLIT_ASYNC;
    }
    if (my_schedule < SCHEDULE_MAX) {
        if (schedule_list[my_direction][my_schedule].schedule_name != NULL) {
            printf("%s\n", schedule_list[my_direction][my_schedule].schedule_name);
            ret = (*schedule_list[my_direction][my_schedule].schedule_call)(input, output);
        } else {
            printf("Schedule pipeline test not built-in (%s, %s)\n", dma_direction, schedule);
            ret = -2;
        }
    } else {
        printf("Incorrect input Correct schedule: basic, fold, async, split, split_async\n");
        ret = -1;
    }

    if (ret != 0) {
        printf("pipeline failed! %d\n", ret);
    } else {
        // verify result by comparing to expected values
        int error_count = 0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                for (int z = 0; z < 4; z++) {
                    uint8_t correct = data_in[x * 4 + z + y * width * 4] * 2;
                    uint8_t result = (!strcmp(dma_direction, "rw")) ? data_out[x * 4 + z + y * width * 4] : output(x, y, z);
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

    if (!strcmp(dma_direction, "rw")) {
        halide_hexagon_dma_unprepare(nullptr, output);
    }

    // DMA_step 5: Processing is completed and ready to exit, deallocate the DMA engine
    halide_hexagon_dma_deallocate_engine(nullptr, dma_engine);

    if ((!strcmp(schedule, "async") || !strcmp(schedule, "split_async")) && !strcmp(dma_direction, "rw")) {
        halide_hexagon_dma_deallocate_engine(nullptr, dma_engine_write);
    }

    free(data_in);
    free(data_out);

    return ret;
}
