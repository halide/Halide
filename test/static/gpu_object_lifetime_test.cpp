#include <func_gpu_object_lifetime.h>
#include <static_image.h>
#include <math.h>
#include <stdio.h>
#include <HalideRuntime.h>
#include <assert.h>
#include "../common/gpu_object_lifetime.h"

extern "C" void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    record_gpu_debug(str);
}

int main(int argc, char **argv) {

    const int W = 80;
    const int H = 80;

    // Run the whole program several times.
    for (int i = 0; i < 2; i++) {
        Image<int> input(W, H);
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y) = x + y;
            }
        }
        input.set_host_dirty();

        Image<int> output(W, H);

        printf("Evaluating output over %d x %d\n", W, H);
        func_gpu_object_lifetime(input, output);

        output.copy_to_host();
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                if (2*input(x, y) != output(x, y)) {
                    printf("Error! %d != 2*%d at %d, %d\n", output(x, y), input(x, y), x, y);
                    return -1;
                }
            }
        }

        printf("Releasing\n");
        halide_release(NULL);
    }

    int ret = validate_gpu_object_lifetime(false /* allow_globals */);
    if (ret != 0) {
        return ret;
    }

    printf("Success!\n");
    return 0;
}
