#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "HalideRuntimeMetal.h"

#include "metal_completion_handler_override.h"

struct MyUserContext {
    int counter;

    MyUserContext()
        : counter(0) {
    }
};

extern "C" int halide_metal_command_buffer_completion_handler(void *user_context, struct halide_metal_command_buffer *, char **) {
    auto ctx = (MyUserContext *)user_context;
    ctx->counter++;
    return halide_error_code_success;
}

int main(int argc, char *argv[]) {
#if defined(TEST_METAL)
    Halide::Runtime::Buffer<int32_t> output(32, 32);

    MyUserContext my_context;
    metal_completion_handler_override(&my_context, output);
    output.copy_to_host();

    // Check the output
    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (output(x, y) != x + y * 2) {
                printf("Error: output(%d, %d) = %d instead of %d\n", x, y, output(x, y), x + y * 2);
                return -1;
            }
        }
    }

    if (my_context.counter < 1) {
        printf("Error: completion handler was not called\n");
        return -1;
    }

    printf("Success!\n");
#else
    printf("[SKIP] Metal not enabled\n");
#endif
    return 0;
}