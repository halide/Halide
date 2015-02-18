#include <math.h>
#include <stdio.h>
#include <assert.h>

#include "HalideRuntime.h"
#include "static_image.h"
#include "user_context.h"

// Standard header file doesn't declare the _jit_wrapper entry point.
// Note that user_context is specified in arg[0].
extern "C" int user_context_jit_wrapper(void** args);

static const void *context_pointer = (void *)0xf00dd00d;

static bool called_error = false;
static bool called_trace = false;
static bool called_malloc = false;
static bool called_free = false;

extern "C" void halide_error(void *context, const char *msg) {
    called_error = true;
    assert(context == context_pointer);
}

extern "C" int32_t halide_trace(void *context, const halide_trace_event *e) {
    called_trace = true;
    assert(context == context_pointer);
    return 0;
}

extern "C" void *halide_malloc(void *context, size_t sz) {
    assert(context == context_pointer);
    called_malloc = true;
    return malloc(sz);
}

extern "C" void halide_free(void *context, void *ptr) {
    assert(context == context_pointer);
    called_free = true;
    free(ptr);
}

int main(int argc, char **argv) {
    int result;

    Image<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y) = 1;
        }
    }
    Image<float> output(10, 10);

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

    // verify that calling via the _jit_wrapper entry point
    // also produces the correct result
    const void* arg0 = context_pointer;
    buffer_t arg1 = *input;
    buffer_t arg2 = *output;
    void* args[3] = { &arg0, &arg1, &arg2 };
    called_error = false;
    called_trace = false;
    called_malloc = false;
    called_free = false;
    result = user_context_jit_wrapper(args);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }
    assert(called_malloc && called_free);
    assert(called_trace && !called_error);

    Image<float> big_output(11, 11);
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
