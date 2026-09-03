#ifndef HALIDE_APP_BENCH_HARNESS_H
#define HALIDE_APP_BENCH_HARNESS_H

// Standardized benchmarking protocol shared by the inductive-function apps
// (viterbi, prefixsum, chebyshev, ode, the alignment and biquads runners, mamba). The point is
// that every app reports numbers the same way, so the paper's tables are directly
// comparable and self-documenting.
//
// Protocol (override via environment):
//   HB_WARMUP  (default 3)   untimed warmup iterations
//   HB_TRIALS  (default 30)  timed iterations; we report the BEST (minimum) time
//                            (as Halide's benchmark() does), which is the run
//                            least perturbed by OS/thermal noise. Median is kept
//                            as a reference column to show the spread.
//
// Each timed iteration runs a caller-supplied `body` that performs exactly one
// full realize. On GPU, work is queued asynchronously, so the batched form
// runs `batch` bodies and then a `finish` (a device sync) inside the timed
// region and divides by the batch: the time includes device completion but
// not a synchronization per launch, which for a sub-millisecond kernel would
// be most of the measurement. HB_BATCH overrides the batch size.
//
// Memory is reported ANALYTICALLY (byte-exact, deterministic): the app passes the
// live bytes of the recurrence state/trajectory for each variant. This isolates
// the quantity storage-folding controls from inputs/outputs (which are identical
// across variants and must not be counted as a win). See print_row / mem_ratio.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace hb {

inline int env_int(const char *k, int dflt) {
    const char *v = getenv(k);
    return (v && *v) ? atoi(v) : dflt;
}

// Ablation helper: returns `full` when UNFOLD is set in the environment, else
// `folded`. Lets any inductive app expose an "inductive WITHOUT folding"
// baseline by pinning fold_storage's factor to the full recurrence-axis extent
// -- which materializes the whole trajectory and also bypasses Halide's
// automatic storage folding -- while holding the fusion/compute placement fixed.
// So the folded-vs-unfolded delta isolates folding from fusion.
inline int fold_factor(int folded, int full) {
    return getenv("UNFOLD") ? full : folded;
}

struct Stats {
    double median = 0, p25 = 0, p75 = 0, min = 0;
    int trials = 0;
};

// Run `body` HB_WARMUP + HB_TRIALS times; return median/IQR/min over the timed
// runs, per body. A trial is `batch` bodies followed by `finish`, timed
// together and divided by the batch; the default is one body and no finish.
inline Stats bench(const std::function<void()> &body, int batch,
                   const std::function<void()> &finish, int warmup = -1, int trials = -1) {
    if (warmup < 0) warmup = env_int("HB_WARMUP", 3);
    if (trials < 0) trials = env_int("HB_TRIALS", 30);
    trials = std::max(1, trials);
    batch = std::max(1, env_int("HB_BATCH", batch));
    auto trial = [&]() {
        for (int i = 0; i < batch; i++)
            body();
        if (finish)
            finish();
    };
    for (int i = 0; i < warmup; i++)
        trial();
    std::vector<double> ms;
    ms.reserve(trials);
    for (int i = 0; i < trials; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        trial();
        auto t1 = std::chrono::high_resolution_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count() / batch);
    }
    std::sort(ms.begin(), ms.end());
    Stats s;
    s.trials = trials;
    s.min = ms.front();
    s.median = ms[trials / 2];
    s.p25 = ms[trials / 4];
    s.p75 = ms[(3 * trials) / 4];
    return s;
}

inline Stats bench(const std::function<void()> &body) {
    return bench(body, 1, nullptr);
}

// The best time in seconds, for runners that report their own rows.
inline double bench_s(const std::function<void()> &body, int batch = 1,
                      const std::function<void()> &finish = nullptr,
                      int warmup = -1, int trials = -1) {
    return bench(body, batch, finish, warmup, trials).min * 1e-3;
}
// One untimed-free single shot, for interleaved diagnostic rounds.
inline double bench_once_s(const std::function<void()> &body) {
    return bench(body, 1, nullptr, 0, 1).min * 1e-3;
}

// Machine + protocol provenance, emitted once per result file so §6.1 of the
// paper is generated rather than transcribed. `target` is the Halide target
// string (pass "host" for CPU JIT). Optional `note` for app-specific config.
inline void print_spec_header(const char *app, const std::string &target,
                              const std::string &note = "") {
    unsigned hw = std::thread::hardware_concurrency();
    const char *hlt = getenv("HL_NUM_THREADS");
    printf("### %s  |  target=%s  |  hw_concurrency=%u  |  HL_NUM_THREADS=%s\n",
           app, target.c_str(), hw, hlt ? hlt : "(default)");
    printf("### protocol: warmup=%d trials=%d batch=%d  metric=best(min) ms; median shown for reference\n",
           env_int("HB_WARMUP", 3), env_int("HB_TRIALS", 30), env_int("HB_BATCH", 1));
    if (!note.empty()) printf("### %s\n", note.c_str());
    printf("### %-30s %10s %10s   %12s  %10s  %s\n",
           "variant", "best_ms", "median_ms", "throughput", "state_MB", "check");
}

inline double mb(double bytes) {
    return bytes / (1024.0 * 1024.0);
}

// Last-level-cache size in bytes, for the roofline x-axis (unfolded footprint /
// LLC). Honors HB_LLC_BYTES if set; otherwise probes sysfs for the largest CPU
// cache (index3=L3, falling back to L2), and finally a 32 MB default. Detected
// once. This is the cache boundary the recurrence/batch sweeps are designed to
// cross, so folding's win/tie/loss can be read off a single normalized axis.
inline double llc_bytes() {
    static double cached = -1.0;
    if (cached > 0) return cached;
    if (const char *e = getenv("HB_LLC_BYTES")) {
        double v = atof(e);
        if (v > 0) return cached = v;
    }
    double best = 0;
    for (int idx = 3; idx >= 0 && best == 0; idx--) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu0/cache/index%d/size", idx);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        double num = 0;
        char unit = 0;
        if (fscanf(f, "%lf%c", &num, &unit) >= 1) {
            best = num * (unit == 'M' ? 1024 * 1024 : unit == 'K' ? 1024 :
                                                                    1);
        }
        fclose(f);
    }
    return cached = (best > 0 ? best : 32.0 * 1024 * 1024);
}

// Unfolded footprint / LLC -- the roofline x-axis. Points from BOTH the
// recurrence-length sweep and the batch-axis sweep collapse onto this axis.
inline double footprint_over_llc(double unfolded_bytes) {
    return unfolded_bytes / llc_bytes();
}

// One standardized result row. `tput` is a caller-computed throughput (e.g.
// Mtok/s, Mpix/s) with its unit already folded into `tput_unit`; compute it from
// s.min (the reported best time) so the row is self-consistent. `state_bytes`
// is the analytic recurrence-state footprint for THIS variant. `err`/`ok` are
// the correctness gate. `verdict` is one honest word: match|win|tie|lose|--.
// `unfolded_bytes` is the ROOFLINE x-axis input: the footprint of the recurrence
// state when NOT folded (identical across a shape's variants). When >= 0 the row
// gains a trailing "fp/LLC=<ratio>" field so the collapse plot (throughput vs
// unfolded-footprint/LLC) can be built by grepping result rows directly. Pass -1
// (default) to omit it, e.g. for apps whose footprint story isn't fold-relevant.
inline void print_row(const char *variant, const Stats &s,
                      double tput, const char *tput_unit,
                      double state_bytes, double err, bool ok,
                      const char *verdict = "", double unfolded_bytes = -1) {
    // A negative err signals the correctness check was not run (e.g. the CPU
    // reference is opt-in at large sizes) -- report UNCHECKED, never a bare PASS.
    const char *status = (err < 0) ? "UNCHECKED" : (ok ? "PASS" : "FAIL");
    char errbuf[32];
    if (err < 0) errbuf[0] = '\0';
    else
        snprintf(errbuf, sizeof(errbuf), " (err %.2g)", err);
    char fpbuf[48];
    if (unfolded_bytes < 0) fpbuf[0] = '\0';
    else
        snprintf(fpbuf, sizeof(fpbuf), "  fp/LLC=%.3f", footprint_over_llc(unfolded_bytes));
    // state_bytes is also emitted BYTE-EXACT as a trailing key=value: the %.2f MB
    // column floors sub-10KB and zero footprints to 0.00, which breaks a log-scale
    // memory plot. This field is analytic/deterministic (1 trial, no warmup needed).
    printf("  %-30s %10.3f %10.3f   %8.1f %-4s %10.2f  %s%s%s%s%s  state_bytes=%.0f\n",
           variant, s.min, s.median, tput, tput_unit,
           mb(state_bytes), status, errbuf,
           verdict[0] ? "  " : "", verdict, fpbuf, state_bytes);
}

// Convenience for the memory-scaling story: ratio of two analytic footprints.
inline double mem_ratio(double non_inductive_bytes, double inductive_bytes) {
    return inductive_bytes > 0 ? non_inductive_bytes / inductive_bytes : 0.0;
}

// Honest speed verdict for an inductive variant vs its baseline, from the
// measured (best) times, with a 2% noise band. Never hard-code "win": a tag must
// never contradict the best_ms columns it sits next to.
inline const char *verdict(double inductive_ms, double baseline_ms) {
    if (inductive_ms < baseline_ms * 0.98) return "win";
    if (inductive_ms > baseline_ms * 1.02) return "lose";
    return "tie";
}

}  // namespace hb

#endif  // HALIDE_APP_BENCH_HARNESS_H
