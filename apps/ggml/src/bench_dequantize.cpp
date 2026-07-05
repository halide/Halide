#include "benchmarks.h"

#include <ggml.h>

#include "compare.h"
#include "data_gen.h"
#include "timing.h"

namespace {
constexpr int64_t kTargetElements = 4096;
}  // namespace

BenchReport run_dequantize_benchmarks(const KernelRegistries &registries) {
    BenchReport report{"dequantize_row", "GB/s", {}};
    print_report_header(report.title, report.throughput_unit);

    for (int t = 0; t < GGML_TYPE_COUNT; ++t) {
        const ggml_type type = static_cast<ggml_type>(t);
        const Impl<dequantize_fn_t> *ref = registries.dequantize.reference(type);
        const Impl<quantize_fn_t> *qref = registries.quantize.reference(type);
        if (!ref || !qref) {
            continue;
        }
        ggml_quantize_init(type);  // one-time, cheap after the first call for this type; see ggml_provider.cpp

        const int64_t blck = ggml_blck_size(type);
        const int64_t n = ((kTargetElements + blck - 1) / blck) * blck;

        AlignedBuffer src(n * sizeof(float));
        generate_synthetic_data(src.as<float>(), n);

        AlignedBuffer quantized(ggml_row_size(type, n));
        qref->fn(src.as<float>(), quantized.data(), n);

        AlignedBuffer ref_out(n * sizeof(float));
        ref->fn(quantized.data(), ref_out.as<float>(), n);
        const TimingResult ref_time = time_calls([&] { ref->fn(quantized.data(), ref_out.as<float>(), n); });

        BenchRow row;
        row.label = ggml_type_name(type);
        row.ref_name = ref->name;
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = bytes_per_sec(n * sizeof(float), ref_time.median_ns) / 1e9;

        for (const auto &cand : registries.dequantize.candidates(type)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == ref->fn);
            if (!bc.identical) {
                AlignedBuffer cand_out(n * sizeof(float));
                cand.fn(quantized.data(), cand_out.as<float>(), n);
                bc.correct = floats_match(ref_out.as<float>(), cand_out.as<float>(), n);

                const TimingResult cand_time = time_calls([&] { cand.fn(quantized.data(), cand_out.as<float>(), n); });
                bc.ns = cand_time.median_ns;
                bc.throughput = bytes_per_sec(n * sizeof(float), cand_time.median_ns) / 1e9;
                bc.speedup = ref_time.median_ns / cand_time.median_ns;
            }
            row.candidates.push_back(bc);
        }

        print_row(row, report.throughput_unit);
        report.rows.push_back(std::move(row));
    }

    return report;
}
