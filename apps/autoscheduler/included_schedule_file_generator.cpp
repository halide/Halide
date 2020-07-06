#include "Halide.h"

#if defined(GENERATING_SCHEDULE)
// nothing
#else
#include "included_schedule_file.schedule.h"
#endif

namespace {

// Trivial Generator for testing (and demonstrating) use of .schedule.h
// files produced by the autoschedulers; this is very similar to
// demo_generator.cpp, but packaged separately to avoid confusion for
// newcomers.
struct IncludedScheduleFile : public Halide::Generator<IncludedScheduleFile> {
    Input<Buffer<float>> input{"input", 4};
    Input<Buffer<float>> filter{"filter", 4};
    Input<Buffer<float>> bias{"bias", 1};
    Output<Buffer<float>> relu{"relu", 4};

    void generate() {
        const int N = 5, CI = 120, CO = 24, W = 100, H = 80;

        Var x("x"), y("y"), c("c"), n("n");

        // Algorithm
        Func conv("conv");
        RDom r(0, CI, 0, 3, 0, 3);
        conv(c, x, y, n) = bias(c);
        conv(c, x, y, n) += filter(c, r.y, r.z, r.x) * input(r.x, x + r.y, y + r.z, n);
        relu(c, x, y, n) = max(0, conv(c, x, y, n));

        // Estimates (for autoscheduler and/or RunGen)
        input.set_estimates({{0, CI}, {0, W + 2}, {0, H + 2}, {0, N}});
        filter.set_estimates({{0, CO}, {0, 3}, {0, 3}, {0, CI}});
        bias.set_estimates({{0, CO}});
        relu.set_estimates({{0, CO}, {0, W}, {0, H}, {0, N}});

        // Schedule
        if (auto_schedule) {
            // nothing
        } else {
#if defined(GENERATING_SCHEDULE)
            abort();
#else
            apply_schedule_included_schedule_file(get_pipeline(), get_target());
#endif
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IncludedScheduleFile, included_schedule_file)
