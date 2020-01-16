#include <assert.h>
#include <atomic>
#include <math.h>
#include <stdio.h>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "user_context_insanity.h"

using namespace Halide::Runtime;

const int num_launcher_tasks = 1000;

static std::atomic<bool> got_context[num_launcher_tasks] = {};

int32_t my_halide_trace(void *context, const halide_trace_event_t *e) {
    std::atomic<bool> *bool_ptr = (std::atomic<bool> *)context;
    *bool_ptr = true;
    return 0;
}

int launcher_task(void *user_context, int index, uint8_t *closure) {
    Buffer<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y) = (float)(x * y);
        }
    }
    Buffer<float> output(10, 10);

    user_context_insanity(&got_context[index], input, output);

    return 0;
}

int main(int argc, char **argv) {
    halide_set_custom_trace(my_halide_trace);

    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    halide_do_par_for(nullptr, launcher_task, 0, num_launcher_tasks, nullptr);

    for (int i = 0; i < num_launcher_tasks; ++i)
        assert(got_context[i] == true);

    printf("Success!\n");
    return 0;
}
