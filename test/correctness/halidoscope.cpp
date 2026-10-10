// Exercises Pipeline::halidoscope() and Generator::halidoscope(). Each check runs the real trace and
// profile instrumentation, but avoids depending on the actual Halidoscope
// GUI binary being built or installed anywhere in this environment by
// pointing HalidoscopeOptions::path at this test binary itself,
// re-exec'd with the same "--trace <path> --stmt <path> [--profile <path>]" argv shape
// Internal::pipeline_halidoscope() would pass to the real thing -- mirroring
// test/correctness/run_process.cpp.
//
// halidoscope() serializes and deserializes the pipeline internally, so it's
// only meaningful when Halide was built with serialization support;
// TEST_WITH_SERIALIZATION is defined by CMake in that case.

#include "Halide.h"
#include "halide_trace_compression.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Halide;

#ifdef TEST_WITH_SERIALIZATION

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

// Calls halidoscope() from within generate(), in the way selected by `mode`.
class HalidoscopeGen : public Generator<HalidoscopeGen> {
public:
    Input<Buffer<uint8_t, 2>> input{"input"};
    Input<int32_t> offset{"offset"};
    Output<Buffer<int32_t, 2>> output{"output"};

    enum Mode { Plain,
                WithContext,
                SeparateArgs,
                WrongType,
                WrongCount } mode = Plain;
    HalidoscopeOptions options;
    Buffer<uint8_t> in;
    Buffer<int32_t> out;
    Buffer<int32_t> trace_out;
    JITUserContext user_context;

    void generate() {
        Var x("x"), y("y");
        Func f("f");
        f(x, y) = cast<int32_t>(input(x, y)) + offset;
        output(x, y) = f(x, y) * 2;
        f.compute_root();

        switch (mode) {
        case Plain:
            halidoscope(options, in, 5, out);
            break;
        case WithContext:
            halidoscope(options, &user_context, in, 7, out);
            break;
        case SeparateArgs:
            halidoscope(options, {in, 11, trace_out}, {in, 13, out});
            break;
        case WrongType:
            halidoscope(options, in, 5.0f, out);
            break;
        case WrongCount:
            halidoscope(options, in, out);
            break;
        }
    }
};

}  // namespace

int main(int argc, char **argv) {
    // Stub-launcher mode: stand in for the real Halidoscope GUI binary.
    // Internal::pipeline_halidoscope() invokes its launcher with exactly this argv shape,
    // so re-exec'ing this test binary lets the checks below exercise a real
    // launch without needing the actual GUI app.
    if (argc >= 3 && std::string(argv[1]) == "--trace") {
        return 0;
    }

    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        std::cout << "[SKIP] WebAssembly JIT does not support the profiler or tracing to a file.\n";
        return 0;
    }

    const std::string self = fs::absolute(argv[0]).string();

    // Happy path: one traced realization and one profiled realization
    // should be written to output_dir, and the "launch" (our
    // stub) should succeed without halidoscope() throwing.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.path = self;
        options.output_dir = dir;

        p.halidoscope({16, 16}, options);

        std::string trace_path = dir + "/trace.hltrace";
        std::string profile_path = dir + "/profile.json";
        check(Internal::file_exists(trace_path));
        check(Internal::file_exists(profile_path));

        // The trace should decompress to stores of both Funcs over the
        // whole 16x16 domain.
        std::vector<char> trace = slurp(trace_path);
        int f_stores = 0, g_stores = 0;
        std::vector<uint8_t> packets;
        check(Tools::decompress_trace((const uint8_t *)trace.data(), trace.size(), packets));
        for (size_t pos = 0; pos < packets.size();) {
            const halide_trace_packet_t &packet = *(const halide_trace_packet_t *)(packets.data() + pos);
            if (packet.event == halide_trace_store) {
                f_stores += std::string(packet.func()) == "f" ? packet.lanes : 0;
                g_stores += std::string(packet.func()) == "g" ? packet.lanes : 0;
            }
            pos += packet.size;
        }
        check(f_stores >= 16 * 16);
        check(g_stores >= 16 * 16);

        std::vector<char> profile_json = slurp(profile_path);
        check(contains(profile_json, "\"pipelines\": ["));
        check(contains(profile_json, "\"name\": \"f\""));

        std::string stmt_path = dir + "/conceptual_stmt.html";
        check(contains(slurp(stmt_path), "<html"));

        Internal::file_unlink(trace_path);
        Internal::file_unlink(profile_path);
        Internal::file_unlink(dir + "/conceptual_stmt.html");
        Internal::dir_rmdir(dir);
    }

    // profile_runs == 0 should skip the profiling run entirely
    // (no profile.json), while still writing the trace as usual.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.path = self;
        options.output_dir = dir;
        options.profile_runs = 0;

        p.halidoscope({16, 16}, options);

        check(Internal::file_exists(dir + "/trace.hltrace"));
        check(!Internal::file_exists(dir + "/profile.json"));

        Internal::file_unlink(dir + "/trace.hltrace");
        Internal::file_unlink(dir + "/conceptual_stmt.html");
        Internal::dir_rmdir(dir);
    }

    // The Buffer-realization overload should behave the same as the sizes
    // overload.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.path = self;
        options.output_dir = dir;

        Buffer<int> out(16, 16);
        p.halidoscope(out, options);

        check(Internal::file_exists(dir + "/trace.hltrace"));
        check(Internal::file_exists(dir + "/profile.json"));

        Internal::file_unlink(dir + "/trace.hltrace");
        Internal::file_unlink(dir + "/profile.json");
        Internal::file_unlink(dir + "/conceptual_stmt.html");
        Internal::dir_rmdir(dir);
    }

    // Generator::halidoscope, called from generate(), takes the same
    // arguments as the Callable from compile_to_callable, optionally
    // preceded by a HalidoscopeOptions.
    {
        const GeneratorContext context(get_jit_target_from_environment());

        std::string dir = Internal::dir_make_temp();
        HalidoscopeOptions options;
        options.path = self;
        options.output_dir = dir;

        Buffer<uint8_t> in(16, 16);
        in.fill(3);
        Buffer<int32_t> out(16, 16);
        Buffer<int32_t> trace_out(4, 4);

        auto run = [&](HalidoscopeGen::Mode mode) {
            auto gen = HalidoscopeGen::create(context);
            gen->mode = mode;
            gen->options = options;
            gen->in = in;
            gen->out = out;
            gen->trace_out = trace_out;
            static_cast<Internal::AbstractGenerator &>(*gen).build_pipeline();
        };

        run(HalidoscopeGen::Plain);

        std::string trace_path = dir + "/trace.hltrace";
        std::string profile_path = dir + "/profile.json";
        check(Internal::file_exists(trace_path));
        check(Internal::file_exists(profile_path));
        check(slurp(trace_path).size() > 1024);
        check(contains(slurp(profile_path), "\"name\": \"f$"));
        check(out(0, 0) == (3 + 5) * 2);

        out.fill(0);
        run(HalidoscopeGen::WithContext);
        check(out(0, 0) == (3 + 7) * 2);

        // Separate arguments for the tracing run and the profiling runs.
        out.fill(0);
        run(HalidoscopeGen::SeparateArgs);
        check(trace_out(0, 0) == (3 + 11) * 2);
        check(out(0, 0) == (3 + 13) * 2);

        Internal::file_unlink(trace_path);
        Internal::file_unlink(profile_path);
        Internal::file_unlink(dir + "/conceptual_stmt.html");
        Internal::dir_rmdir(dir);

        check(exception_thrown([&]() { run(HalidoscopeGen::WrongType); }));
        check(exception_thrown([&]() { run(HalidoscopeGen::WrongCount); }));
    }

    // An explicit path that doesn't exist should fail fast,
    // before any instrumentation or launch is attempted.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        HalidoscopeOptions options;
        options.path = "/no/such/path/to/halidoscope";

        check(exception_thrown([&]() { p.halidoscope({16, 16}, options); }));
    }

    // A bare binary name that can't be found on $PATH should fail after
    // trying (and failing) to launch it.
    {
        Func f("f"), g("g");
        Pipeline p = make_test_pipeline(f, g);

        HalidoscopeOptions options;
        options.path = "not_halidoscope";

        check(exception_thrown([&]() { p.halidoscope({16, 16}, options); }));
    }

    std::cout << "Success!\n";
    return 0;
}

#else  // TEST_WITH_SERIALIZATION

int main() {
    std::cout << "[SKIP] halidoscope requires WITH_SERIALIZATION.\n";
    return 0;
}

#endif  // TEST_WITH_SERIALIZATION
