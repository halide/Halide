#include "Halide.h"
#include <cstdio>
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {

    Func f;
    Var x, y;
    f(x, y) = x + y;
    f.parallel(x);

    // Having more threads than tasks shouldn't hurt performance too much.
    double correct_time = 0;

    for (int t = 2; t <= 64; t *= 2) {
        std::ostringstream ss;
        ss << "HL_NUM_THREADS=" << t;
        std::string str = ss.str();
        char buf[32] = {0};
        memcpy(buf, str.c_str(), str.size());
        putenv(buf);
        Halide::Internal::JITSharedRuntime::release_all();
        f.compile_jit();
        // Start the thread pool without giving any hints as to the
        // number of tasks we'll be using.
        f.realize(t, 1);
        double min_time = benchmark(3, 1, [&]() { return f.realize(2, 1000000); });

        printf("%d: %f ms\n", t, min_time * 1e3);
        if (t == 2) {
            correct_time = min_time;
        } else if (min_time > correct_time * 5) {
            printf("Unacceptable overhead when using %d threads for 2 tasks: %f ms vs %f ms\n",
                   t, min_time, correct_time);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
