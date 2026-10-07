#include "Halide.h"

#include <algorithm>
#include <chrono>
#include <stdio.h>
#include <vector>

using namespace Halide;

// Measures the fork/join overhead of small parallel loops separated by serial
// work, with and without Halide::ThreadPoolKeepAwake. Timing on shared CI
// machines is too noisy to assert on, so this only reports the numbers.

namespace {

using Clock = std::chrono::steady_clock;

void busy_wait(int us) {
    auto start = Clock::now();
    while (Clock::now() - start < std::chrono::microseconds(us)) {
    }
}

// Median time per call of a parallel loop of num_tasks trivial tasks, in
// microseconds, with gap_us of serial work between calls.
double fork_join_us(Callable &c, Buffer<int> &out, int gap_us) {
    const int samples = 2000;
    std::vector<double> times;
    times.reserve(samples);
    for (int i = 0; i < samples + 100; i++) {
        auto t0 = Clock::now();
        if (c(out) != 0) {
            printf("Pipeline failed\n");
            exit(1);
        }
        auto t1 = Clock::now();
        if (i >= 100) {
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        busy_wait(gap_us);
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Var x;
    Func f;
    f(x) = x * 2;
    f.parallel(x);
    Callable c = f.compile_to_callable({}, t);

    // One task per thread. The thread pool picks its size on first use.
    Buffer<int> out(1);
    if (c(out) != 0) {
        printf("Pipeline failed\n");
        return 1;
    }
    const int num_threads = std::max(Internal::JITSharedRuntime::get_num_threads(), 1);
    out = Buffer<int>(num_threads);

    printf("threads %d\n", num_threads);
    printf("%8s %14s %14s\n", "gap (us)", "default (us)", "awake (us)");
    for (int gap : {0, 20, 100}) {
        double asleep = fork_join_us(c, out, gap);
        double awake;
        {
            ThreadPoolKeepAwake keep_awake;
            awake = fork_join_us(c, out, gap);
        }
        printf("%8d %14.2f %14.2f\n", gap, asleep, awake);
    }

    printf("Success!\n");
    return 0;
}
