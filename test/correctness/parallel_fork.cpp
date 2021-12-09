#include "Halide.h"
#include "halide_benchmark.h"

#include <atomic>
#include <chrono>
#include <stdio.h>
#include <thread>

using namespace Halide;
using namespace Halide::Tools;

std::atomic<int32_t> call_count{0};

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT int five_ms(int arg) {
    call_count++;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return arg;
}

namespace halide_externs {
HalideExtern_1(int, five_ms, int);
}

enum Schedule {
    Serial,
    Parallel,
    AsyncRoot,
    AsyncComputeAt,
};

Func make(Schedule schedule) {
    Var x("x"), y("y"), z("z");
    std::string suffix = "_" + std::to_string((int)schedule);
    Func both("both" + suffix), f("f" + suffix), g("g" + suffix);

    f(x, y) = halide_externs::five_ms(x + y);
    g(x, y) = halide_externs::five_ms(x - y);

    both(x, y, z) = select(z == 0, f(x, y), g(x, y));

    both.compute_root().bound(z, 0, 2);
    switch (schedule) {
    case Serial:
        f.compute_root();
        g.compute_root();
        break;
    case Parallel:
        both.parallel(z);
        f.compute_at(both, z);
        g.compute_at(both, z);
        break;
    case AsyncRoot:
        f.compute_root().async();
        g.compute_root().async();
        break;
    case AsyncComputeAt:
        both.parallel(z);
        f.compute_at(both, z).async();
        g.compute_at(both, z).async();
        break;
    }

    return both;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Skipping test for WebAssembly as it does not support async() yet.\n");
        return 0;
    }

    Func both;
    Buffer<int32_t> im;
    int count;
    double time;

    call_count = 0;
    both = make(Serial);
    im = both.realize({10, 10, 2});
    count = call_count;
    time = benchmark([&]() {
        both.realize(im);
    });
    printf("Serial time %f for %d calls.\n", time, count);
    fflush(stdout);

    call_count = 0;
    both = make(Parallel);
    im = both.realize({10, 10, 2});
    count = call_count;
    time = benchmark([&]() {
        both.realize(im);
    });
    printf("Parallel time %f for %d calls.\n", time, count);
    fflush(stdout);

    both = make(AsyncRoot);
    call_count = 0;
    im = both.realize({10, 10, 2});
    count = call_count;
    time = benchmark([&]() {
        both.realize(im);
    });
    printf("Async root time %f for %d calls.\n", time, count);
    fflush(stdout);

    both = make(AsyncComputeAt);
    call_count = 0;
    im = both.realize({10, 10, 2});
    count = call_count;
    time = benchmark([&]() {
        both.realize(im);
    });
    printf("AsyncComputeAt time %f for %d calls.\n", time, count);
    fflush(stdout);

    printf("Success!\n");
    return 0;
}
