#include "Halide.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

using namespace Halide;

namespace {

bool buffers_equal(const Buffer<int> &a, const Buffer<int> &b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            if (a(x, y) != b(x, y)) {
                fprintf(stderr, "Mismatch at (%d, %d): %d vs %d\n", x, y, a(x, y), b(x, y));
                return false;
            }
        }
    }
    return true;
}

// A two-stage separable blur over a synthetic input defined everywhere (so no
// boundary handling is needed). Returns the output Func.
Func make_blur(Var x, Var y) {
    Func input("input"), blur_x("blur_x"), blur_y("blur_y");
    input(x, y) = x * 2 + y;
    blur_x(x, y) = (input(x - 1, y) + input(x, y) + input(x + 1, y)) / 3;
    blur_y(x, y) = (blur_x(x, y - 1) + blur_x(x, y) + blur_x(x, y + 1)) / 3;
    return blur_y;
}

const int W = 64, H = 48;

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }
    load_plugin(argv[1]);

    const Target target = get_jit_target_from_environment();
    Var x("x"), y("y");

    // Author a schedule with ScheduleEditor and serialize it to JSON.
    std::string json;
    {
        Func blur_y = make_blur(x, y);
        ScheduleEditor ed({blur_y});
        ed.schedule("blur_y").split("y", "yo", "yi", 8).parallel("yo").vectorize("x", 8);
        ed.schedule("blur_x").compute_at("blur_y", "yo").vectorize("x", 8);
        json = ScheduleAnalyzer::to_json(ed.directives());
    }

    // Reference result with the default (all-inline) schedule.
    Buffer<int> reference = make_blur(x, y).realize({W, H});

    // Apply the JSON schedule to a fresh pipeline through the plugin and confirm
    // the schedule took effect and the result is unchanged.
    {
        Func blur_y = make_blur(x, y);
        Pipeline p({blur_y});
        AutoschedulerParams params = {"JsonSchedule", {{"json", json}}};
        AutoSchedulerResults result = p.apply_autoscheduler(target, params);

        assert(result.schedule_source.find("blur_y.split(y, yo, yi, 8)") != std::string::npos);
        assert(result.schedule_source.find("blur_x.compute_at(blur_y, yo)") != std::string::npos);

        Buffer<int> out = blur_y.realize({W, H});
        assert(buffers_equal(out, reference) && "JsonSchedule changed the result");
    }

    // The same schedule delivered via a file path (autoscheduler.json_path=...).
    {
        const std::string path = std::string(argv[0]) + ".schedule.json";
        std::ofstream(path) << json;

        Func blur_y = make_blur(x, y);
        Pipeline p({blur_y});
        AutoschedulerParams params = {"JsonSchedule", {{"json_path", path}}};
        p.apply_autoscheduler(target, params);
        Buffer<int> out = blur_y.realize({W, H});
        assert(buffers_equal(out, reference) && "JsonSchedule (json_path) changed the result");
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    // Missing schedule and unknown params are reported (only observable with
    // exceptions enabled; otherwise the errors abort).
    {
        Func f = make_blur(x, y);
        bool threw = false;
        try {
            Pipeline({f}).apply_autoscheduler(target, {"JsonSchedule", {}});
        } catch (const Halide::Error &) {
            threw = true;
        }
        assert(threw && "expected an error when no json/json_path is given");
    }
    {
        Func f = make_blur(x, y);
        bool threw = false;
        try {
            Pipeline({f}).apply_autoscheduler(target, {"JsonSchedule", {{"json", json}, {"bogus", "1"}}});
        } catch (const Halide::Error &) {
            threw = true;
        }
        assert(threw && "expected an error for unknown params");
    }
#endif

    printf("Success!\n");
    return 0;
}
