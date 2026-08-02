// From-scratch Halide reimplementation of GGML's "repack" quantize_mat
// kernels (see src/ggml-cpu/repack.cpp: ggml_quantize_mat_q8_0_4x4_generic /
// ggml_quantize_mat_q8_0_4x8_generic / ggml_quantize_mat_q8_K_4x4_generic /
// ggml_quantize_mat_q8_K_4x8_generic upstream, as of GGML v0.15.3). These
// take 4 contiguous rows of `k` floats (row r at x[r*k .. r*k+k)) and
// interleave them into ONE activation-format block per `k`-sized chunk,
// where 4 per-row values are laid out consecutively (in groups of
// `blck_size_interleave`) instead of one row's worth at a time -- this is
// the packed activation format the corresponding repack_gemv/repack_gemm
// kernels consume. There are only 4 distinct interleavings (2 activation
// formats x 2 interleave widths), reused across every repack weight type
// that shares that (activation, interleave) pair -- see k_repack_entries in
// providers/ggml_provider.cpp and this file's registration in
// halide_provider.cpp.
//
// block_q8_0x4 layout (136 bytes, one per 32-element chunk x 4 rows):
//   byte 0-7:    4 fp16 deltas, one per row, in row order
//   byte 8-135:  128 signed int8 quants, interleaved in groups of
//                `blck_size_interleave` (4 or 8) per row
//
// block_q8_Kx4 layout (1168 bytes, one per 256-element chunk x 4 rows):
//   byte 0-15:    4 float32 deltas, one per row, in row order
//   byte 16-1039: 1024 signed int8 quants, interleaved the same way
//   byte 1040-1167: 64 signed int16 "bsums" (sum of quants in groups of 16,
//                  scattered across rows/groups by the same index mapping
//                  GGML uses -- see index_q8_k below)
//
// Q8_0's per-row scale is the same amax/127 symmetric scale as plain Q8_0
// (see quant_components.h's make_symmetric_block_scheme()), using round()
// (roundf, not round-to-even). Q8_K's per-row scale is the same -127/max
// signed scale as plain Q8_K (see quant_components.h's make_q8_k_scheme()),
// using the same round-to-nearest-even magic-number trick -- but unlike
// plain Q8_K's quantize_row, this repack version has no final MIN(127, ...)
// clamp (safe here since the scale is derived from this exact block's own
// amax, so values can never exceed +-127 already).
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "quant_components.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

constexpr int kQK8_0 = 32;
constexpr int kBlockBytesQ8_0x4 = 4 * 2 + kQK8_0 * 4;  // 136

constexpr int kQK_K = 256;
constexpr int kNumGroups = kQK_K / 16;                                     // 16
constexpr int kBlockBytesQ8_Kx4 = 4 * 4 + kQK_K * 4 + kNumGroups * 4 * 2;  // 1168

// Shared "quantize_mat" pipeline: a 2-D activation x(col, row in [0,4)) flows
// through the codec `scheme` (block-relayout + Q8 quantize + interleave) via
// the same approximate_by/compute_offline idiom as codec_generator_base.h's
// Quantize direction -- the encode half is adopted as the output block buffer.
struct QuantizeMatPipe {
    ImageParam x;
    Func blocks_out;
};
inline QuantizeMatPipe build_quantize_mat(std::unique_ptr<Approximation> scheme, int block_bytes) {
    ImageParam x(Float(32), 2, "x");  // dim 0: col-within-row (mult. of block), dim 1: row (4)
    Var col("col"), row("row"), byte("byte"), ib("ib");
    Func identity("qm_identity");
    identity(col, row) = x(col, row);

    ApproximationResult r = Func(x).approximate_by(*scheme, {identity});
    for (Func h : r.handles) {
        if (h.has_update_definition()) {
            h.compute_root();
        }
    }

    ImageParam blocks_in(UInt(8), 2, "blocks_in");
    ComputeOfflineResult q = Pipeline({identity}).compute_offline(r.encoded, {blocks_in});

    Func blocks_out("blocks");
    blocks_out(byte, ib) = q.offline.outputs()[0](byte, ib);
    blocks_out.output_buffer().dim(0).set_bounds(0, block_bytes);
    blocks_out.output_buffer().dim(1).set_min(0);
    x.dim(0).set_min(0);
    x.dim(1).set_bounds(0, 4);
    return {x, blocks_out};
}

// q8_0_4x4 / q8_0_4x8 differ only by interleave width (blocklen).
template<int Blocklen>
class Q8_0QuantizeMatGenerator : public Generator<Q8_0QuantizeMatGenerator<Blocklen>> {
public:
    void configure() {
        QuantizeMatPipe p = build_quantize_mat(make_q8_0x4_scheme(Blocklen), kBlockBytesQ8_0x4);
        this->add_input(p.x);
        this->add_output(p.blocks_out);
    }
    void generate() {
    }
};

// q8_k_4x4 / q8_k_4x8: same shared pipeline, the Q8_K interleaved codec.
template<int Blocklen>
class Q8_KQuantizeMatGenerator : public Generator<Q8_KQuantizeMatGenerator<Blocklen>> {
public:
    void configure() {
        QuantizeMatPipe p = build_quantize_mat(make_q8_kx4_scheme(Blocklen), kBlockBytesQ8_Kx4);
        this->add_input(p.x);
        this->add_output(p.blocks_out);
    }
    void generate() {
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q8_0QuantizeMatGenerator<4>, q8_0_4x4_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_0QuantizeMatGenerator<8>, q8_0_4x8_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_KQuantizeMatGenerator<4>, q8_k_4x4_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_KQuantizeMatGenerator<8>, q8_k_4x8_quantize_mat)
