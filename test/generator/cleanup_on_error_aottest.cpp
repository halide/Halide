#include "HalideRuntime.h"

// Grab the internal device_interface functions
#define WEAK
#include "device_interface.h"

#include <stdio.h>
#include <stdlib.h>

#include "cleanup_on_error.h"
#include "static_image.h"

const int size = 64;

int successful_mallocs = 0, failed_mallocs = 0, frees = 0, errors = 0, device_mallocs = 0, device_frees = 0;

extern "C" void *halide_malloc(void *user_context, size_t x) {
    // Only the first malloc succeeds
    if (successful_mallocs) {
        failed_mallocs++;
        return NULL;
    }
    successful_mallocs++;

    void *orig = malloc(x+40);
    // Round up to next multiple of 32. Should add at least 8 bytes so we can fit the original pointer.
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

extern "C" void halide_free(void *user_context, void *ptr) {
    frees++;
    free(((void**)ptr)[-1]);
}

extern "C" void halide_error(void *user_context, const char *msg) {
    errors++;
}

extern "C" int halide_device_free(void *user_context, struct buffer_t *buf) {
    device_frees++;
    const halide_device_interface *interface = halide_get_device_interface(buf->dev);
    return interface->device_free(user_context, buf);
}

extern "C" int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    device_mallocs++;
    return interface->device_malloc(user_context, buf);
}


int main(int argc, char **argv) {

    Image<int32_t> output(size);
    int result = cleanup_on_error(output);

    if (result != -1) {
        printf("The exit status was %d instead of -1\n", result);
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

    if (errors != 2) {
        // There's one error from the malloc failing, and one error from the pipeline failing.
        printf("There were supposed to be two errors\n");
        return -1;
    }

    if (device_mallocs != device_frees) {
        printf("There were a different number of device mallocs (%d) and frees (%d)\n", device_mallocs, device_frees);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
