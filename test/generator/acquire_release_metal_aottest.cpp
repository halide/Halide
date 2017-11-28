#include <stdio.h>

#if defined(__APPLE__) && defined(__MACH__) && defined(TEST_METAL)

#include <math.h>
#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include "HalideRuntimeMetal.h"
#include <assert.h>
#include <string.h>
#include <objc/message.h>

#include "acquire_release.h"

extern "C" {

bool acquire_command_buffer_called = false;
bool release_command_buffer_called = false;
halide_metal_command_buffer *saved_command_buffer = NULL;
halide_metal_command_queue *saved_command_queue = NULL;

int halide_metal_acquire_command_buffer(void *user_context,
                                         halide_metal_command_queue *queue,
                                         halide_metal_command_buffer **buffer_ret) {
    printf("Custom halide_metal_acquire_command_buffer() called\n");
    acquire_command_buffer_called = true;
    if (queue == saved_command_queue && saved_command_buffer != NULL) {
        printf("\tReturning previously-created command buffer\n");
        *buffer_ret = saved_command_buffer;
    } else {
        printf("\tCreating command buffer\n");
        *buffer_ret = (halide_metal_command_buffer*)objc_msgSend((objc_object*)queue, sel_getUid("commandBuffer"));
        objc_msgSend((objc_object*)(*buffer_ret), sel_getUid("retain"));
        saved_command_buffer = *buffer_ret;
        saved_command_queue = queue;

        if (!(*buffer_ret)) {
            return -1;
        }
    }

    return 0;
}

int halide_metal_release_command_buffer(void *user_context,
                                        halide_metal_command_queue *queue,
                                        halide_metal_command_buffer **command_buffer,
                                        bool must_release) {
    printf("Custom halide_metal_release_command_buffer() called\n");
    if (must_release) {
        printf("\tmust_release is true\n");
        release_command_buffer_called = true;
        objc_msgSend((objc_object*)(*command_buffer), sel_getUid("commit"));
        objc_msgSend((objc_object*)(*command_buffer), sel_getUid("release"));
        *command_buffer = NULL;
        saved_command_buffer = NULL;
        saved_command_queue = NULL;
    } else {
        printf("\tmust_release is not true, so not releasing\n");
    }

    return 0;
}

} // extern "C"


using namespace Halide::Runtime;

const int W = 256, H = 256;

int main(int argc, char **argv) {

    // Everything else is a normal Halide program. The GPU runtime will call
    // the above acquire/release functions to get the context instead of using
    // its own internal context.
    Buffer<float> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(x * y);
        }
    }

    input.set_host_dirty(true);

    Buffer<float> output(W, H);

    acquire_release(input, output);

    output.copy_to_host();

    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (input(x, y) * 2.0f + 1.0f != output(x, y)) {
                printf("Error at (%d, %d): %f != %f\n", x, y, input(x, y) * 2.0f + 1.0f,
                       output(x, y));
                return -1;
            }
        }
    }

    // We need to free our GPU buffers before destroying the context.
    input.device_free();
    output.device_free();

    if (!acquire_command_buffer_called || !release_command_buffer_called) {
      printf("Custom acquire/release not called for command buffer\n");
      printf("FAILED\n");
      return -1;
    }

    printf("Success!\n");
    return 0;
}

#else
// This test requires macOS/Metal
int main(int argc, char **argv) {
  printf("Skipping test on non-macOS/non-Metal platform\n");
  return 0;
}
#endif
