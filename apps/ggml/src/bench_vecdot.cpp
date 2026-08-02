#include "benchmarks.h"

#include <ggml-cpu.h>
#include <ggml.h>

#include "compare.h"
#include "data_gen.h"
#include "timing.h"

namespace {
// Divisible by every quant block size in play (32 for the q4/q5/q8 family, 256 for the k-quants).
constexpr int64_t kElements = 4096;
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
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = bytes_per_sec(vx.size() + vy.size(), ref_time.median_ns) / 1e9;

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
                bc.ns = cand_time.median_ns;
                bc.throughput = bytes_per_sec(vx.size() + vy.size(), cand_time.median_ns) / 1e9;
                bc.speedup = ref_time.median_ns / cand_time.median_ns;
            }
            row.candidates.push_back(bc);
        }

        print_row(row, report.throughput_unit);
        report.rows.push_back(std::move(row));
    }

    return report;
}
