#include "benchmarks.h"

#include <cstring>
#include <vector>

#include <ggml-alloc.h>
#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <ggml.h>

#include "compare.h"
#include "data_gen.h"
#include "ggml_internal_abi.h"  // ggml_backend_cpu_repack_buffer_type()
#include "timing.h"

namespace {

// K (reduction dim): divisible by every block size in play (32 and 256).
constexpr int64_t kK = 4096;
// Output columns: divisible by every NB_COLS in play (4 and 8).
constexpr int kNC = 32;
// Activation rows for the gemm (batched) path; must be a multiple of 4 (the
// row-group size ggml_quantize_mat_* always packs), and > 3 so production
// code would pick gemm over gemv for this many rows (see repack.cpp's
// forward_mul_mat_one_chunk: "if there are more than three rows in src1,
// use gemm; otherwise, use gemv").
constexpr int kGemmRows = 32;

// Builds a packed weight buffer for `base_type`, shape [kK, kNC], by
// quantizing a row-major staging buffer through the reference quantizer and
// then letting the repack buffer type's set_tensor callback do the actual
// interleaving (src/ggml-cpu/repack.cpp:4733). This avoids reimplementing
// the private block<K,N> interleave layout by hand -- everything here is
// public API plus the one declared-ourselves accessor for the buffer type.
struct PackedWeight {
    ggml_context *ctx = nullptr;
    ggml_backend_buffer_t buffer = nullptr;
    ggml_tensor *tensor = nullptr;

    PackedWeight() = default;
    PackedWeight(const PackedWeight &) = delete;
    PackedWeight &operator=(const PackedWeight &) = delete;
    PackedWeight(PackedWeight &&other) noexcept
        : ctx(other.ctx), buffer(other.buffer), tensor(other.tensor) {
        other.ctx = nullptr;
        other.buffer = nullptr;
        other.tensor = nullptr;
    }
    PackedWeight &operator=(PackedWeight &&other) noexcept {
        if (this != &other) {
            if (buffer) ggml_backend_buffer_free(buffer);
            if (ctx) ggml_free(ctx);
            ctx = other.ctx;
            buffer = other.buffer;
            tensor = other.tensor;
            other.ctx = nullptr;
            other.buffer = nullptr;
            other.tensor = nullptr;
        }
        return *this;
    }
    ~PackedWeight() {
        if (buffer) ggml_backend_buffer_free(buffer);
        if (ctx) ggml_free(ctx);
    }

    // ggml_repack_get_optimal_repack_type() (src/ggml-cpu/repack.cpp) picks
    // ONE interleave layout per (type, CPU features, column count) -- it is
    // GGML's own "best kernel for this hardware" heuristic, not something
    // this benchmark can steer towards a specific registered combo. When it
    // finds none (e.g. Q2_K has no ARM branch at all, only AVX512/RISC-V --
    // see that function), the buffer type's init_tensor callback leaves
    // tensor->extra null, and calling set_tensor on it would dereference a
    // null tensor_traits pointer. There is no supported way to force a
    // different combo through this public mechanism, so such combos are
    // skipped rather than benchmarked with fabricated data.
    bool supported_on_this_cpu() const {
        return tensor && tensor->extra != nullptr;
    }
};

PackedWeight build_packed_weight(ggml_type base_type, const Impl<quantize_fn_t> &row_quant, int64_t k, int nc) {
    PackedWeight pw;
    ggml_init_params params{/*.mem_size=*/ggml_tensor_overhead() + 256, /*.mem_buffer=*/nullptr,
                            /*.no_alloc=*/true};
    pw.ctx = ggml_init(params);
    pw.tensor = ggml_new_tensor_2d(pw.ctx, base_type, k, nc);
    pw.buffer = ggml_backend_alloc_ctx_tensors_from_buft(pw.ctx, ggml_backend_cpu_repack_buffer_type());
    if (!pw.supported_on_this_cpu()) {
        return pw;
    }

    AlignedBuffer staging(ggml_row_size(base_type, k) * nc);
    AlignedBuffer col(k * sizeof(float));
    const size_t row_bytes = ggml_row_size(base_type, k);
    for (int c = 0; c < nc; ++c) {
        generate_synthetic_data(col.as<float>(), k, static_cast<float>(c));
        row_quant.fn(col.as<float>(), staging.as<uint8_t>() + c * row_bytes, k);
    }
    ggml_backend_tensor_set(pw.tensor, staging.data(), 0, staging.size());
    return pw;
}

}  // namespace

std::vector<BenchReport> run_repack_benchmarks(const KernelRegistries &registries) {
    BenchReport quant_mat_report{"repack_quantize_mat", "GB/s", {}};
    BenchReport gemv_report{"repack_gemv", "GFLOP/s", {}};
    BenchReport gemm_report{"repack_gemm", "GFLOP/s", {}};

    print_report_header(quant_mat_report.title, quant_mat_report.throughput_unit);
    for (const RepackKey &key : registries.repack_quantize_mat.keys()) {
        const Impl<quantize_fn_t> *qm_ref = registries.repack_quantize_mat.reference(key.label);
        if (!qm_ref) {
            continue;
        }
        const int64_t blck = ggml_blck_size(key.act_type);
        const int64_t k = ((kK + blck - 1) / blck) * blck;

        // ggml_quantize_mat_* always consumes exactly 4 rows (see
        // src/ggml-cpu/repack.cpp: ggml_quantize_mat_t<...> asserts nrow==4),
        // regardless of the interleave geometry in the name.
        AlignedBuffer src(4 * k * sizeof(float));
        generate_synthetic_data(src.as<float>(), 4 * k);
        const size_t out_bytes = 4 * ggml_row_size(key.act_type, k);

        AlignedBuffer ref_out(out_bytes);
        qm_ref->fn(src.as<float>(), ref_out.data(), k);
        const TimingResult ref_time = time_calls([&] { qm_ref->fn(src.as<float>(), ref_out.data(), k); });

        BenchRow row;
        row.label = key.label;
        row.ref_name = qm_ref->name;
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = bytes_per_sec(4 * k * sizeof(float), ref_time.median_ns) / 1e9;

        for (const auto &cand : registries.repack_quantize_mat.candidates(key.label)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == qm_ref->fn);
            if (!bc.identical) {
                AlignedBuffer cand_out(out_bytes);
                cand.fn(src.as<float>(), cand_out.data(), k);
                bc.correct = (std::memcmp(ref_out.data(), cand_out.data(), out_bytes) == 0);

                const TimingResult cand_time = time_calls([&] { cand.fn(src.as<float>(), cand_out.data(), k); });
                bc.ns = cand_time.median_ns;
                bc.throughput = bytes_per_sec(4 * k * sizeof(float), cand_time.median_ns) / 1e9;
                bc.speedup = ref_time.median_ns / cand_time.median_ns;
            }
            row.candidates.push_back(bc);
        }
        print_row(row, quant_mat_report.throughput_unit);
        quant_mat_report.rows.push_back(std::move(row));
    }

    print_report_header(gemv_report.title, gemv_report.throughput_unit);
    for (const RepackKey &key : registries.repack_gemv.keys()) {
        const Impl<gemx_fn_t> *gemv_ref = registries.repack_gemv.reference(key.label);
        const Impl<quantize_fn_t> *w_quant = registries.quantize.reference(key.base_type);
        const Impl<quantize_fn_t> *a_quant = registries.quantize.reference(key.act_type);
        if (!gemv_ref || !w_quant || !a_quant) {
            continue;
        }
        const int64_t blck = ggml_blck_size(key.base_type);
        const int64_t k = ((kK + blck - 1) / blck) * blck;

        PackedWeight weight = build_packed_weight(key.base_type, *w_quant, k, kNC);
        if (!weight.supported_on_this_cpu()) {
            continue;  // no repack kernel for this (type, CPU) combo -- see PackedWeight::supported_on_this_cpu
        }

        AlignedBuffer a_src(k * sizeof(float));
        generate_synthetic_data(a_src.as<float>(), k, 3.0f);
        AlignedBuffer vy(ggml_row_size(key.act_type, k));
        a_quant->fn(a_src.as<float>(), vy.data(), k);

        AlignedBuffer ref_out(kNC * sizeof(float));
        gemv_ref->fn(k, ref_out.as<float>(), kNC, weight.tensor->data, vy.data(), 1, kNC);
        const TimingResult ref_time =
            time_calls([&] { gemv_ref->fn(k, ref_out.as<float>(), kNC, weight.tensor->data, vy.data(), 1, kNC); });
        const double ref_flops = 2.0 * k * kNC;

        BenchRow row;
        row.label = key.label;
        row.ref_name = gemv_ref->name;
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = gflops(ref_flops, ref_time.median_ns);

        for (const auto &cand : registries.repack_gemv.candidates(key.label)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == gemv_ref->fn);
            if (!bc.identical) {
                AlignedBuffer cand_out(kNC * sizeof(float));
                cand.fn(k, cand_out.as<float>(), kNC, weight.tensor->data, vy.data(), 1, kNC);
                bc.correct = floats_match(ref_out.as<float>(), cand_out.as<float>(), kNC);

                const TimingResult cand_time =
                    time_calls([&] { cand.fn(k, cand_out.as<float>(), kNC, weight.tensor->data, vy.data(), 1, kNC); });
                bc.ns = cand_time.median_ns;
                bc.throughput = gflops(ref_flops, cand_time.median_ns);
                bc.speedup = ref_time.median_ns / cand_time.median_ns;
            }
            row.candidates.push_back(bc);
        }
        print_row(row, gemv_report.throughput_unit);
        gemv_report.rows.push_back(std::move(row));
    }

    print_report_header(gemm_report.title, gemm_report.throughput_unit);
    for (const RepackKey &key : registries.repack_gemm.keys()) {
        const Impl<gemx_fn_t> *gemm_ref = registries.repack_gemm.reference(key.label);
        const Impl<quantize_fn_t> *w_quant = registries.quantize.reference(key.base_type);
        const Impl<quantize_fn_t> *qm_ref = registries.repack_quantize_mat.reference(key.label);
        if (!gemm_ref || !w_quant || !qm_ref) {
            continue;
        }
        const int64_t blck = ggml_blck_size(key.base_type);
        const int64_t k = ((kK + blck - 1) / blck) * blck;

        PackedWeight weight = build_packed_weight(key.base_type, *w_quant, k, kNC);
        if (!weight.supported_on_this_cpu()) {
            continue;  // no repack kernel for this (type, CPU) combo -- see PackedWeight::supported_on_this_cpu
        }

        AlignedBuffer a_src(kGemmRows * k * sizeof(float));
        generate_synthetic_data(a_src.as<float>(), kGemmRows * k, 5.0f);
        const size_t group_bytes = 4 * ggml_row_size(key.act_type, k);
        AlignedBuffer vy(group_bytes * (kGemmRows / 4));
        for (int g = 0; g < kGemmRows / 4; ++g) {
            qm_ref->fn(a_src.as<float>() + g * 4 * k, vy.as<uint8_t>() + g * group_bytes, k);
        }

        AlignedBuffer ref_out(kGemmRows * kNC * sizeof(float));
        gemm_ref->fn(k, ref_out.as<float>(), kNC, weight.tensor->data, vy.data(), kGemmRows, kNC);
        const TimingResult ref_time = time_calls(
            [&] { gemm_ref->fn(k, ref_out.as<float>(), kNC, weight.tensor->data, vy.data(), kGemmRows, kNC); });
        const double ref_flops = 2.0 * k * kNC * kGemmRows;

        BenchRow row;
        row.label = key.label;
        row.ref_name = gemm_ref->name;
        row.ref_ns = ref_time.median_ns;
        row.ref_throughput = gflops(ref_flops, ref_time.median_ns);

        for (const auto &cand : registries.repack_gemm.candidates(key.label)) {
            BenchCandidate bc;
            bc.name = cand.name;
            bc.identical = (cand.fn == gemm_ref->fn);
            if (!bc.identical) {
                AlignedBuffer cand_out(kGemmRows * kNC * sizeof(float));
                cand.fn(k, cand_out.as<float>(), kNC, weight.tensor->data, vy.data(), kGemmRows, kNC);
                bc.correct = floats_match(ref_out.as<float>(), cand_out.as<float>(), kGemmRows * kNC);

                const TimingResult cand_time = time_calls(
                    [&] { cand.fn(k, cand_out.as<float>(), kNC, weight.tensor->data, vy.data(), kGemmRows, kNC); });
                bc.ns = cand_time.median_ns;
                bc.throughput = gflops(ref_flops, cand_time.median_ns);
                bc.speedup = ref_time.median_ns / cand_time.median_ns;
            }
            row.candidates.push_back(bc);
        }
        print_row(row, gemm_report.throughput_unit);
        gemm_report.rows.push_back(std::move(row));
    }

    return {quant_mat_report, gemv_report, gemm_report};
}
