// Helpers for the JIT-driven benchmarks: a reusing allocator for the timed
// realizes, and the peak memory of one realize as the profiler reports it.
#ifndef HB_JIT_SUPPORT_H
#define HB_JIT_SUPPORT_H

#include "Halide.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace hb {

// A reusing allocator for the timed realizes: a pipeline's scratch buffers
// are freed at the end of every realize, and past a few MB glibc hands them
// back to the kernel, so each timed run would otherwise pay a first-touch
// page fault per 4 KB of them. Blocks are matched by exact size and never
// returned.
struct ReusePool {
    static std::mutex &mtx() { static std::mutex m; return m; }
    static std::vector<std::pair<size_t, void *>> &pool() {
        static std::vector<std::pair<size_t, void *>> p;
        return p;
    }
    static void *alloc(size_t size) {
        constexpr size_t header = 128;
        {
            std::lock_guard<std::mutex> g(mtx());
            for (auto &e : pool()) {
                if (e.first == size && e.second) {
                    void *base = e.second;
                    e.second = nullptr;
                    return (char *)base + header;
                }
            }
        }
        char *base = (char *)aligned_alloc(128, (size + header + 127) / 128 * 128);
        ((size_t *)base)[0] = size;
        return base + header;
    }
    static void dealloc(void *p) {
        if (!p) return;
        char *base = (char *)p - 128;
        size_t size = ((size_t *)base)[0];
        std::lock_guard<std::mutex> g(mtx());
        for (auto &e : pool()) {
            if (!e.second) {
                e = {size, base};
                return;
            }
        }
        pool().emplace_back(size, base);
    }
};
inline void *jit_reuse_malloc(Halide::JITUserContext *, size_t x) {
    return ReusePool::alloc(x);
}
inline void jit_reuse_free(Halide::JITUserContext *, void *p) {
    ReusePool::dealloc(p);
}
inline void reuse_jit_allocations(Halide::Func &f) {
    // HB_NO_REUSE=1 leaves the process allocator in charge, to measure
    // what the pool is worth.
    if (getenv("HB_NO_REUSE")) {
        return;
    }
    f.jit_handlers().custom_malloc = jit_reuse_malloc;
    f.jit_handlers().custom_free = jit_reuse_free;
}

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
