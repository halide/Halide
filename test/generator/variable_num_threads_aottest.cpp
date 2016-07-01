#include "HalideRuntime.h"

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

#include "variable_num_threads.h"

bool stop = false;
int max_threads = 1;

void mess_with_num_threads(void *) {
    while (!stop) {
        halide_set_num_threads((rand() % max_threads) + 1);
    }
}

int main(int argc, char **argv) {

    halide_set_num_threads(1);

    // In one thread we'll run a job with lots of nested parallelism,
    // and in another we'll mess with the number of threads we want
    // running. The intent is to hunt for deadlocks.

    halide_thread *t = halide_spawn_thread(&mess_with_num_threads, NULL);

    buffer_t out = {0};

    out.host = (uint8_t *)malloc(64*64*sizeof(float));
    out.elem_size = sizeof(float);
    out.extent[0] = 64;
    out.stride[0] = 1;
    out.extent[1] = 64;
    out.stride[1] = 64;

    for (int i = 0; i < 1000; i++) {
        // The number of threads will oscilate randomly, but the range
        // will slowly ramp up and back down so you can watch it
        // working in a process monitor.
        max_threads = 1 + std::min(i, 1000-i) / 50;
        int ret = variable_num_threads(&out);
        if (ret) {
            printf("Non zero exit code: %d\n", ret);
            return -1;
        }
    }

    stop = true;
    halide_join_thread(t);

    printf("Success\n");
    return 0;
}
