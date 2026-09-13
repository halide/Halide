#include "benchmarks.h"

#include <ggml-cpu.h>
#include <ggml.h>

#include "compare.h"
#include "data_gen.h"
#include "timing.h"

#include <cstdlib>
#include <cstring>

namespace {
// Divisible by every quant block size in play (32 for the q4/q5/q8 family, 256 for the k-quants).
// Dev iteration aid: KERNEL_BENCH_N overrides it (must stay a multiple of 256),
// which is how per-call overhead is separated from per-block cost.
int64_t elements() {
    const char *n = std::getenv("KERNEL_BENCH_N");
    return (n && *n) ? std::atoll(n) : 4096;
}
const int64_t kElements = elements();

// Dev iteration aid: KERNEL_BENCH_FILTER=q4_0,q8_0 restricts the sweep to
// matching type names (substring match, comma-separated). Empty => all.
bool passes_filter(const char *name) {
    const char *f = std::getenv("KERNEL_BENCH_FILTER");
    if (!f || !*f) {
        return true;
    }
    std::string filt(f), item;
    size_t pos = 0;
    while (pos <= filt.size()) {
        size_t comma = filt.find(',', pos);
        if (comma == std::string::npos) comma = filt.size();
        item = filt.substr(pos, comma - pos);
        if (!item.empty() && std::strstr(name, item.c_str())) {
            return true;
        }
        pos = comma + 1;
    }
    return false;
}
}  // namespace

BenchReport run_vecdot_benchmarks(const KernelRegistries &registries) {
    BenchReport report{"vec_dot", "GB/s", {}};
    print_report_header(report.title, report.throughput_unit);

    for (int t = 0; t < GGML_TYPE_COUNT; ++t) {
        const ggml_type type = static_cast<ggml_type>(t);
        const Impl<vec_dot_fn_t> *ref = registries.vec_dot.reference(type);
        if (!ref) {
            continue;
        }
        if (!passes_filter(ggml_type_name(type))) {
            continue;
        }

        const ggml_type_traits_cpu *tc = ggml_get_type_traits_cpu(type);
        if (!tc) {
            continue;
        }
        const ggml_type act_type = tc->vec_dot_type;

        const Impl<quantize_fn_t> *x_quant = registries.quantize.reference(type);
        const Impl<quantize_fn_t> *y_quant = registries.quantize.reference(act_type);
        if (!x_quant || !y_quant) {
            continue;  // shouldn't happen for any type reachable through the CPU backend
        }
        ggml_quantize_init(type);  // one-time, cheap after the first call for this type; see ggml_provider.cpp

        AlignedBuffer x_src(kElements * sizeof(float));
        AlignedBuffer y_src(kElements * sizeof(float));
        generate_synthetic_data(x_src.as<float>(), kElements, 0.0f);
        generate_synthetic_data(y_src.as<float>(), kElements, 7.0f);  // different phase so x != y

        AlignedBuffer vx(ggml_row_size(type, kElements));
        AlignedBuffer vy(ggml_row_size(act_type, kElements));
        x_quant->fn(x_src.as<float>(), vx.data(), kElements);
        y_quant->fn(y_src.as<float>(), vy.data(), kElements);

        float ref_result = 0.0f;
        ref->fn(kElements, &ref_result, 0, vx.data(), 0, vy.data(), 0, 1);
        const TimingResult ref_time =
            time_calls([&] { ref->fn(kElements, &ref_result, 0, vx.data(), 0, vy.data(), 0, 1); });

        BenchRow row;
        row.label = ggml_type_name(type);
        row.ref_name = ref->name;
        row.ref_ns = ref_time.min_ns;
        row.ref_throughput = bytes_per_sec(vx.size() + vy.size(), ref_time.min_ns) / 1e9;

        for (const auto &cand : registries.vec_dot.candidates(type)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == ref->fn);
            if (!bc.identical) {
                float cand_result = 0.0f;
                cand.fn(kElements, &cand_result, 0, vx.data(), 0, vy.data(), 0, 1);
                bc.correct = floats_match(&ref_result, &cand_result, 1);

                const TimingResult cand_time =
                    time_calls([&] { cand.fn(kElements, &cand_result, 0, vx.data(), 0, vy.data(), 0, 1); });
                bc.ns = cand_time.min_ns;
                bc.throughput = bytes_per_sec(vx.size() + vy.size(), cand_time.min_ns) / 1e9;
                bc.speedup = ref_time.min_ns / cand_time.min_ns;
            }
            row.candidates.push_back(bc);
        }

        print_row(row, report.throughput_unit);
        report.rows.push_back(std::move(row));
    }

    return report;
}
