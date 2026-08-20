// Exercises Pipeline::halidoscope(). Each check runs the real trace and
// profile instrumentation, but avoids depending on the actual Halidoscope
// GUI binary being built or installed anywhere in this environment by
// pointing HalidoscopeOptions::halidoscope_path at this test binary itself,
// re-exec'd with the same "--trace <path> [--profile <path>]" argv shape
// halidoscope_impl() would pass to the real thing -- mirroring
// test/correctness/run_process.cpp.

#include "Halide.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Halide;

namespace {

namespace fs = std::filesystem;

// Return 1 from main() on failure; the test harness treats that as a failure.
// (assert() is compiled out in release builds, so we can't rely on it.)
#define check(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::cerr << "FAILED: " #cond " (line " << __LINE__ << ")\n"; \
            return 1;                                                     \
        }                                                                 \
    } while (0)

std::vector<char> slurp(const std::string &path) {
    return Internal::read_entire_file(path);
}

bool contains(const std::vector<char> &haystack, const std::string &needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

// A minimal 2-stage pipeline: enough for halidoscope()'s tracing and
// profiling to have more than one Func to report on.
Pipeline make_test_pipeline(Func &f, Func &g) {
    Var x("x"), y("y");
    f(x, y) = x + y;
    g(x, y) = f(x, y) * 2;
    f.compute_root();
    return Pipeline(g);
}

bool exception_thrown(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const Error &) {
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char **argv) {
    // Stub-launcher mode: stand in for the real Halidoscope GUI binary.
    // halidoscope_impl() invokes its launcher with exactly this argv shape,
    // so re-exec'ing this test binary lets the checks below exercise a real
    // launch without needing the actual GUI app.
    if (argc >= 3 && std::string(argv[1]) == "--trace") {
        return 0;
    }

    const std::string self = fs::absolute(argv[0]).string();

    // Happy path: one traced realization and one profiled realization
    // should be written to halidoscope_output_dir, and the "launch" (our
    // stub) should succeed without halidoscope() throwing.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.halidoscope_path = self;
        options.halidoscope_output_dir = dir;

        p.halidoscope({16, 16}, options);

        std::string trace_path = dir + "/trace.hltrace";
        std::string profile_path = dir + "/profile.json";
        check(Internal::file_exists(trace_path));
        check(Internal::file_exists(profile_path));

        // Every load/store/realization event the tracing hook writes is at
        // least the size of one halide_trace_packet_t; a realization of two
        // Funcs over a 16x16 domain produces many such events, so a
        // near-empty file would indicate tracing silently didn't run.
        check(slurp(trace_path).size() > 1024);

        std::vector<char> profile_json = slurp(profile_path);
        check(contains(profile_json, "\"pipelines\":["));
        check(!contains(profile_json, "\"funcs\":[]"));

        Internal::file_unlink(trace_path);
        Internal::file_unlink(profile_path);
        Internal::dir_rmdir(dir);
    }

    // halidoscope_profile_runs == 0 should skip the profiling run entirely
    // (no profile.json), while still writing the trace as usual.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.halidoscope_path = self;
        options.halidoscope_output_dir = dir;
        options.halidoscope_profile_runs = 0;

        p.halidoscope({16, 16}, options);

        check(Internal::file_exists(dir + "/trace.hltrace"));
        check(!Internal::file_exists(dir + "/profile.json"));

        Internal::file_unlink(dir + "/trace.hltrace");
        Internal::dir_rmdir(dir);
    }

    // The Buffer-realization overload should behave the same as the sizes
    // overload.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.halidoscope_path = self;
        options.halidoscope_output_dir = dir;

        Buffer<int> out(16, 16);
        p.halidoscope(out, options);

        check(Internal::file_exists(dir + "/trace.hltrace"));
        check(Internal::file_exists(dir + "/profile.json"));

        Internal::file_unlink(dir + "/trace.hltrace");
        Internal::file_unlink(dir + "/profile.json");
        Internal::dir_rmdir(dir);
    }

    // An explicit halidoscope_path that doesn't exist should fail fast,
    // before any instrumentation or launch is attempted.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        HalidoscopeOptions options;
        options.halidoscope_path = "/no/such/path/to/halidoscope";

        check(exception_thrown([&]() { p.halidoscope({16, 16}, options); }));
    }

    // A bare binary name that can't be found on $PATH should fail after
    // trying (and failing) to launch it.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        HalidoscopeOptions options;
        options.halidoscope_path = "not_halidoscope";

        check(exception_thrown([&]() { p.halidoscope({16, 16}, options); }));
    }

    std::cout << "Success!\n";
    return 0;
}
