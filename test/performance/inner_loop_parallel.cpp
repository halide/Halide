#include <Halide.h>
#include <stdio.h>
#include "clock.h"

using namespace Halide;

int main(int argc, char **argv) {

    Func f;
    Var x, y;
    f(x, y) = x + y;
    f.parallel(x);

    // Having more threads than tasks shouldn't hurt performance too much.
    double correct_time = 0;

    for (int t = 2; t <= 64; t *= 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "HL_NUM_THREADS=%d", t);
        putenv(buf);
        Halide::Internal::JITSharedRuntime::release_all();
        f.compile_jit();
        double min_time = 1e20;
        for (int i = 0; i < 5; i++) {
            double t1 = current_time();
            f.realize(2, 100000);
            double t2 = current_time();
            min_time = std::min(min_time, t2 - t1);
        }

        printf("%d: %f ms\n", t, min_time);
        if (t == 2) {
            correct_time = min_time;
        } else if (min_time > correct_time * 2) {
            printf("Unacceptable overhead when using %d threads for 2 tasks: %f ms vs %f ms\n",
                   t, min_time, correct_time);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
