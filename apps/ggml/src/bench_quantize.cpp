#include "benchmarks.h"

#include <cstring>

#include <ggml.h>

#include "data_gen.h"
#include "timing.h"

namespace {
constexpr int64_t kTargetElements = 4096;
}

BenchReport run_quantize_benchmarks(const KernelRegistries &registries) {
    BenchReport report{"quantize_row", "GB/s", {}};
    print_report_header(report.title, report.throughput_unit);

    for (int t = 0; t < GGML_TYPE_COUNT; ++t) {
        const ggml_type type = static_cast<ggml_type>(t);
        const Impl<quantize_fn_t> *ref = registries.quantize.reference(type);
        if (!ref) {
            continue;
        }
        ggml_quantize_init(type);  // one-time, cheap after the first call for this type; see ggml_provider.cpp

        const int64_t blck = ggml_blck_size(type);
        const int64_t n = ((kTargetElements + blck - 1) / blck) * blck;
        const size_t out_bytes = ggml_row_size(type, n);

        AlignedBuffer src(n * sizeof(float));
        generate_synthetic_data(src.as<float>(), n);

        AlignedBuffer ref_out(out_bytes);
        ref->fn(src.as<float>(), ref_out.data(), n);
        const TimingResult ref_time = time_calls([&] { ref->fn(src.as<float>(), ref_out.data(), n); });

        BenchRow row;
        row.label = ggml_type_name(type);
        row.ref_name = ref->name;
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = bytes_per_sec(n * sizeof(float), ref_time.median_ns) / 1e9;

        for (const auto &cand : registries.quantize.candidates(type)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == ref->fn);
            if (!bc.identical) {
                AlignedBuffer cand_out(out_bytes);
                cand.fn(src.as<float>(), cand_out.data(), n);
                bc.correct = (std::memcmp(ref_out.data(), cand_out.data(), out_bytes) == 0);

                const TimingResult cand_time = time_calls([&] { cand.fn(src.as<float>(), cand_out.data(), n); });
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
