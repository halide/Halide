#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Func f;
    Var x, y;
    f(x, y) = x + y;
    f.parallel(x);

    Pipeline p(f);

    // Having more threads than tasks shouldn't hurt performance too much.
    double correct_time = 0;

    for (int t = 2; t <= 64; t *= 2) {
        std::string str = "HL_NUM_THREADS=" + std::to_string(t);
        char buf[32] = {0};
        memcpy(buf, str.c_str(), str.size());
        if (putenv(buf) != 0) {
            fprintf(stderr, "putenv failed\n");
            return -1;
        }
        p.invalidate_cache();
        Halide::Internal::JITSharedRuntime::release_all();

        p.compile_jit();
        // Start the thread pool without giving any hints as to the
        // number of tasks we'll be using.
        p.realize({t, 1});
        BenchmarkResult r = benchmark([&]() { return p.realize({2, 1000000}); });
        double min_time = r.wall_time;

        printf("%d: %f ms (samples %lld iterations %lld accuracy %f)\n",
               t, min_time * 1e3, (long long)r.samples, (long long)r.iterations, r.accuracy);
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
