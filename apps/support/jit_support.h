// Helpers for the JIT-driven benchmarks: the peak memory of one realize as
// the profiler reports it.
#ifndef HB_JIT_SUPPORT_H
#define HB_JIT_SUPPORT_H

#include "Halide.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace hb {

// The peak memory the pipeline's Funcs held during one realize, from the
// profiler: the pipeline is compiled with profiling on and run once, with
// the runtime's JSON report directed to a file, and the pipeline's
// memory_peak read back from it. The text report is silenced. Untimed, and
// separate from the benchmark loop; a later realize without profiling
// recompiles.
inline void silent_print(Halide::JITUserContext *, const char *) {
}

template<typename T>
double profiled_peak_bytes(Halide::Func &f, Halide::Buffer<T> &out) {
    using namespace Halide;
    std::string path = "/tmp/hb_profile_" + std::to_string(getpid()) + ".json";
    setenv("HL_PROFILER_JSON_OUTPUT", path.c_str(), 1);
    auto saved_print = f.jit_handlers().custom_print;
    f.jit_handlers().custom_print = silent_print;
    Target t = get_jit_target_from_environment().with_feature(Target::Profile);
    f.realize(out, t);
    f.jit_handlers().custom_print = saved_print;
    unsetenv("HL_PROFILER_JSON_OUTPUT");

    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    unlink(path.c_str());
    // The pipeline's own fields come before its funcs', so the first
    // memory_peak in the file is the pipeline's.
    const std::string s = ss.str();
    const std::string key = "\"memory_peak\":";
    size_t i = s.find(key);
    if (i == std::string::npos) {
        fprintf(stderr, "profiled_peak_bytes: no memory_peak in the profiler's report for %s\n",
                f.name().c_str());
        return 0;
    }
    return (double)strtoull(s.c_str() + i + key.size(), nullptr, 10);
}

}  // namespace hb

#endif  // HB_JIT_SUPPORT_H
