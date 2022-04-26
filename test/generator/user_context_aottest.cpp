#include <assert.h>
#include <atomic>
#include <math.h>
#include <stdio.h>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "user_context.h"

using namespace Halide::Runtime;

static void *const context_pointer = (void *)(intptr_t)0xf00dd00d;

static std::atomic<bool> called_error{false};
static std::atomic<bool> called_trace{false};
static std::atomic<bool> called_malloc{false};
static std::atomic<bool> called_free{false};

void my_halide_error(void *context, const char *msg) {
    called_error = true;
    assert(context == context_pointer);
}

int32_t my_halide_trace(void *context, const halide_trace_event_t *e) {
    called_trace = true;
    assert(context == context_pointer);
    return 0;
}

void *my_halide_malloc(void *context, size_t sz) {
    assert(context == context_pointer);
    called_malloc = true;
    return malloc(sz);
}

void my_halide_free(void *context, void *ptr) {
    assert(context == context_pointer);
    called_free = true;
    free(ptr);
}

int main(int argc, char **argv) {
    halide_set_error_handler(&my_halide_error);
    halide_set_custom_malloc(&my_halide_malloc);
    halide_set_custom_free(&my_halide_free);
    halide_set_custom_trace(&my_halide_trace);

    int result;

    Buffer<float, 2> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y) = 1;
        }
    }
    Buffer<float, 2> output(10, 10);

    called_error = false;
    called_trace = false;
    called_malloc = false;
    called_free = false;
    result = user_context(context_pointer, input, output);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }
    assert(called_malloc && called_free);
    assert(called_trace && !called_error);

    // verify that calling via the _argv entry point
    // also produces the correct result
    const void *arg0 = context_pointer;
    void *args[3] = {&arg0, input.raw_buffer(), output.raw_buffer()};
    called_error = false;
    called_trace = false;
    called_malloc = false;
    called_free = false;
    result = user_context_argv(args);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }
    assert(called_malloc && called_free);
    assert(called_trace && !called_error);

    Buffer<float, 2> big_output(11, 11);
    called_error = false;
    called_trace = false;
    called_malloc = false;
    called_free = false;
    result = user_context(context_pointer, input, big_output);
    if (result == 0) {
        fprintf(stderr, "Expected this to fail, but got %d\n", result);
        exit(-1);
    }
    assert(called_error);

    printf("Success!\n");
    return 0;
}
