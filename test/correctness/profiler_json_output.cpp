#include "Halide.h"
#include "halide_test_dirs.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>

using namespace Halide;

void set_env(const char *name, const char *val) {
#ifdef _WIN32
    _putenv_s(name, val);
#else
    setenv(name, val, /*overwrite*/ 1);
#endif
}

std::string report;
void capture_print(JITUserContext *, const char *msg) {
    report += msg;
}

std::string read_file(const std::string &path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment().with_feature(Target::Profile);
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support the profiler.\n");
        return 0;
    }

    Func f("profiler_json_output_f"), g("profiler_json_output_g");
    Var x;
    f(x) = x * 2;
    g(x) = f(x) + 1;
    f.compute_root();
    g.compile_jit(target);

    using SetJSONOutputFn = void (*)(const char *);
    auto set_json_output = (SetJSONOutputFn)Internal::JITSharedRuntime::find_symbol(
        target, "halide_profiler_set_json_output");
    if (!set_json_output) {
        printf("Could not find halide_profiler_set_json_output\n");
        return 1;
    }

    const std::string path = Internal::get_test_tmp_dir() + "profiler_json_output.json";
    Internal::ensure_no_file_exists(path);

    {
        // The path is copied, so the caller's string need not outlive the call.
        std::string tmp = path;
        set_json_output(tmp.c_str());
    }
    g.realize({1000}, target);
    Internal::assert_file_exists(path);
    std::string json = read_file(path);
    for (const char *name : {"\"pipelines\"", f.name().c_str(), g.name().c_str()}) {
        if (json.find(name) == std::string::npos) {
            printf("JSON output is missing %s:\n%s\n", name, json.c_str());
            return 1;
        }
    }

    // Reverting to the environment variable stops writing the file.
    Internal::ensure_no_file_exists(path);
    set_json_output(nullptr);
    g.realize({1000}, target);
    if (Internal::file_exists(path)) {
        printf("JSON output was written after reverting to the environment variable\n");
        return 1;
    }

    // ANSI color codes in the printed report must not leak into the JSON
    // strings. A Func dominated by gathers gets a warning that prints its
    // gather count with a dimmed SI suffix. The warning needs profiling
    // samples, so the work per run is increased until there are some.
    {
        set_env("HL_COLORS", "1");
        Func lut("profiler_json_output_lut"), h("profiler_json_output_h");
        const int n = 1 << 20;
        Param<int> k;
        RDom r(0, k);
        lut(x) = x * 3;
        h(x) = 0;
        h(x) += lut((x * 7 + r * 13) % n) + lut((x * 11 + r * 17) % n);
        lut.compute_root().vectorize(x, 8);
        h.update().vectorize(x, 8);
        h.jit_handlers().custom_print = capture_print;
        h.compile_jit(target);

        set_json_output(path.c_str());
        json.clear();
        for (int work = 1; work <= 1024 && json.find("more vector gathers") == std::string::npos; work *= 2) {
            Internal::ensure_no_file_exists(path);
            report.clear();
            k.set(work);
            h.realize({n}, target);
            json = read_file(path);
        }
        set_json_output(nullptr);
        set_env("HL_COLORS", "");

        if (report.find('\033') == std::string::npos) {
            printf("Expected colors in the printed report:\n%s\n", report.c_str());
            return 1;
        }
        if (json.find("more vector gathers") == std::string::npos) {
            printf("JSON output is missing the gather warning:\n%s\n", json.c_str());
            return 1;
        }
        for (char c : json) {
            if ((unsigned char)c < 0x20 && c != '\n') {
                printf("JSON output contains control character %d:\n%s\n", c, json.c_str());
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
