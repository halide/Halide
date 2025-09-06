#include "Halide.h"
#include "halide_benchmark.h"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace Halide;
using namespace Halide::Tools;

namespace {

std::atomic<int32_t> call_count{0};

extern "C" HALIDE_EXPORT_SYMBOL int five_ms(int arg) {
    ++call_count;
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

}  // namespace

class ParallelForkTest : public ::testing::TestWithParam<Schedule> {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "Skipping test for WebAssembly as it does not support async() yet.";
        }
    }
};

TEST_P(ParallelForkTest, ScheduleTest) {
    Schedule schedule = GetParam();

    call_count = 0;
    Var x("x"), y("y"), z("z");
    Func both("both"), f("f"), g("g");

    f(x, y) = halide_externs::five_ms(x + y);
    g(x, y) = halide_externs::five_ms(x - y);

    both(x, y, z) = select(z == 0, f(x, y), g(x, y));

    both.compute_root().bound(z, 0, 2);

    std::string schedule_name = std::to_string(schedule);
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

    Buffer<int32_t> im = both.realize({10, 10, 2});

    double time = benchmark([&] {
        both.realize(im);
    });

    RecordProperty("schedule", schedule_name);
    RecordProperty("runtime_ms", time * 1000.0);
    RecordProperty("call_count", call_count);

    printf("%s time %f for %d calls.\n", schedule_name.c_str(), time, call_count.load());
    fflush(stdout);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    ParallelForkTest,
    ::testing::Values(Serial, Parallel, AsyncRoot, AsyncComputeAt));
