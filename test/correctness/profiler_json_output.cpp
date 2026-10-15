#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>

using namespace Halide;

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

    printf("Success!\n");
    return 0;
}
