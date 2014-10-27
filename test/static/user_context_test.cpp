#include <user_context.h>
#include <../../include/HalideRuntime.h>
#include <static_image.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

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
    Image<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
          input(x, y) = 1;
        }
    }
    Image<float> output(10, 10);

    user_context(input, context_pointer, output);
    assert(called_malloc && called_free);
    assert(called_trace && !called_error);

    Image<float> big_output(11, 11);
    user_context(input, context_pointer, big_output);
    assert(called_error);

    printf("Success!\n");
    return 0;
}
