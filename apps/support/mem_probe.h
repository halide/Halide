#ifndef HB_MEM_PROBE_H
#define HB_MEM_PROBE_H

// Peak internal-scratch measurement for the standardized benches.
//
// state_MB in the results table is a MEASURED high-water mark of the heap that a
// pipeline allocates internally during one realize() -- not an analytic guess.
// It is captured by a custom Halide allocator that records live/peak bytes.
//
// IMPORTANT: this is deliberately kept OUT of the timed path. The custom
// allocator adds a mutex per allocation, so it must never run inside hb::bench().
// Each test benchmarks with the default allocator, then does ONE extra untimed
// realize() through this probe to read the footprint. User-supplied input/output
// Buffers are allocated outside realize(), so they are not counted -- the probe
// sees exactly the pipeline's internal recurrence-state scratch, which is the
// footprint the fold ablation is about.

// Include Halide.h before this header to get the JIT helpers, and/or
// HalideRuntime.h (pulled in by any AOT pipeline header) for the AOT helpers.
// This header intentionally does not include Halide.h itself, so AOT drivers
// that only have the runtime can still use measure_aot_peak().
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace hb {

struct MemProbe {
    static std::mutex &mtx() {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<void *, size_t> &sizes() {
        static std::unordered_map<void *, size_t> s;
        return s;
    }
    static size_t &live() {
        static size_t v = 0;
        return v;
    }
    static size_t &peak() {
        static size_t v = 0;
        return v;
    }
    static void reset() {
        std::lock_guard<std::mutex> g(mtx());
        sizes().clear();
        live() = 0;
        peak() = 0;
    }
    static void *alloc(size_t x) {
        void *p = nullptr;
        // 128-byte aligned so Halide's aligned vector loads/stores are legal.
        if (posix_memalign(&p, 128, x ? x : 1) != 0) return nullptr;
        std::lock_guard<std::mutex> g(mtx());
        sizes()[p] = x;
        live() += x;
        if (live() > peak()) peak() = live();
        return p;
    }
    static void dealloc(void *p) {
        if (!p) return;
        std::lock_guard<std::mutex> g(mtx());
        auto it = sizes().find(p);
        if (it != sizes().end()) {
            live() -= it->second;
            sizes().erase(it);
        }
        free(p);
    }
};

// ---- JIT (only when Halide.h has been included) ----
#ifdef HALIDE_H
inline void *jit_probe_malloc(Halide::JITUserContext *, size_t x) {
    return MemProbe::alloc(x);
}
inline void jit_probe_free(Halide::JITUserContext *, void *p) {
    MemProbe::dealloc(p);
}

// A reusing allocator for the timed realizes: a pipeline's scratch
// buffers are freed at the end of every realize, and past a few MB glibc
// hands them back to the kernel, so each timed run would otherwise pay a
// first-touch page fault per 4 KB of them. Blocks are matched by exact
// size and never returned.
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
    f.jit_handlers().custom_malloc = jit_reuse_malloc;
    f.jit_handlers().custom_free = jit_reuse_free;
}

// Measure the peak internal heap a JIT pipeline allocates during one realize().
// The realize is untimed and separate from the performance loop. Handlers are
// saved and restored so subsequent (timed) realizes use the default allocator.
inline double measure_jit_peak(Halide::Func &f, const std::function<void()> &realize_once) {
    Halide::JITHandlers &h = f.jit_handlers();
    auto saved_malloc = h.custom_malloc;
    auto saved_free = h.custom_free;
    h.custom_malloc = jit_probe_malloc;
    h.custom_free = jit_probe_free;
    MemProbe::reset();
    realize_once();
    double pk = (double)MemProbe::peak();
    h.custom_malloc = saved_malloc;
    h.custom_free = saved_free;
    return pk;
}
#endif  // HALIDE_H

// ---- AOT ----
inline void *aot_probe_malloc(void *, size_t x) {
    return MemProbe::alloc(x);
}
inline void aot_probe_free(void *, void *p) {
    MemProbe::dealloc(p);
}

// Measure the peak internal heap an AOT pipeline allocates during one call. Uses
// the global custom-malloc hook (AOT code does not consult JITHandlers). Restores
// the default hooks afterwards so timed calls are unaffected.
inline double measure_aot_peak(const std::function<void()> &call_once) {
    halide_malloc_t saved_malloc = halide_set_custom_malloc(aot_probe_malloc);
    halide_free_t saved_free = halide_set_custom_free(aot_probe_free);
    MemProbe::reset();
    call_once();
    double pk = (double)MemProbe::peak();
    halide_set_custom_malloc(saved_malloc);
    halide_set_custom_free(saved_free);
    return pk;
}

}  // namespace hb

#endif  // HB_MEM_PROBE_H
