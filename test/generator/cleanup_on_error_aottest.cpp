#include "HalideBuffer.h"
#include "HalideRuntime.h"

// Grab the internal device_interface functions
#define WEAK
#include "device_interface.h"

#include <stdio.h>
#include <stdlib.h>

#include "cleanup_on_error.h"

using namespace Halide::Runtime;

const int size = 64;

int successful_mallocs = 0, failed_mallocs = 0, frees = 0, errors = 0, device_mallocs = 0, device_frees = 0;

void *my_halide_malloc(void *user_context, size_t x) {
    // Only the first malloc succeeds
    if (successful_mallocs) {
        failed_mallocs++;
        return nullptr;
    }
    successful_mallocs++;

    void *orig = malloc(x + 40);
    // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_halide_free(void *user_context, void *ptr) {
    if (!ptr) return;
    frees++;
    free(((void **)ptr)[-1]);
}

void my_halide_error(void *user_context, const char *msg) {
    errors++;
}

#ifndef _WIN32
// These two can't be overridden on windows, so we'll just check that
// the number of calls to free matches the number of calls to malloc.
extern "C" int halide_device_free(void *user_context, struct halide_buffer_t *buf) {
    device_frees++;
    return buf->device_interface->impl->device_free(user_context, buf);
}

extern "C" int halide_device_malloc(void *user_context, struct halide_buffer_t *buf,
                                    const halide_device_interface_t *interface) {
    if (!buf->device) {
        device_mallocs++;
    }
    return interface->impl->device_malloc(user_context, buf);
}
#endif

int main(int argc, char **argv) {

    halide_set_custom_malloc(&my_halide_malloc);
    halide_set_custom_free(&my_halide_free);
    halide_set_error_handler(&my_halide_error);

    Buffer<int32_t, 1> output(size);
    int result = cleanup_on_error(output);

    if (result != halide_error_code_out_of_memory &&
        result != halide_error_code_device_malloc_failed) {
        printf("The exit status was %d instead of %d or %d\n",
               result,
               halide_error_code_out_of_memory,
               halide_error_code_device_malloc_failed);
        return -1;
    }

    if (failed_mallocs != 1) {
        printf("One of the mallocs was supposed to fail\n");
        return -1;
    }

    if (successful_mallocs != 1) {
        printf("One of the mallocs was supposed to succeed\n");
        return -1;
    }

    if (frees != 1) {
        printf("The successful malloc should have been freed\n");
        return -1;
    }

    if (errors != 1) {
        printf("%d errors. There was supposed to be one error\n", errors);
        return -1;
    }

    if (device_mallocs != device_frees) {
        printf("There were a different number of device mallocs (%d) and frees (%d)\n", device_mallocs, device_frees);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
