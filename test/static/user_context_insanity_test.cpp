#include <user_context_insanity.h>
#include <../../include/HalideRuntime.h>
#include <static_image.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

const int num_launcher_tasks = 1000;

static bool got_context[num_launcher_tasks];

extern "C" int32_t halide_trace(void *context, const halide_trace_event *e) {
    bool *bool_ptr = (bool *)context;
    *bool_ptr = true;
    return 0;
}

int launcher_task(void *user_context, int index, uint8_t *closure) {
    Image<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
          input(x, y) = x * y;
        }
    }
    Image<float> output(10, 10);

    int result = user_context_insanity(input, &got_context[index], output);
    if (result != 0) {
        printf("filter failed: %d\n", result);
        return -1;
    }

    return 0;
}


int main(int argc, char **argv) {
    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    halide_do_par_for(NULL, launcher_task, 0, num_launcher_tasks, NULL);

    for (int i = 0; i < num_launcher_tasks; ++i)
        assert(got_context[i] == true);

    printf("Success!\n");
    return 0;
}
