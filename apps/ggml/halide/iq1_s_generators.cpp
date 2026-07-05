// From-scratch Halide reimplementation of GGML's IQ1_S dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq1_s upstream, as of GGML
// v0.15.3). Uses GGML's published iq1s_grid codebook constants verbatim
// (see iq_grids_data.h) -- fixed lookup-table data, not derived logic.
//
// The 50-byte block_iq1_s layout (a 256-element superblock, 8 groups of 32
// elements each):
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-33:  qs[32] -- one grid-index low byte per l (4 l's per group)
//   byte 34-49: qh[8] as 8 little-endian uint16 values, one per group:
//               bits 0-11 (3 bits per l) contribute the grid index's high 3
//               bits, bits 12-14 hold a per-group scale, bit 15 selects a
//               per-group +/- delta added to every dequantized value
//
// Unlike IQ2/IQ3's codebooks (unsigned magnitude + external sign flip),
// iq1s_grid entries are SIGNED bytes used directly (the "sign" is baked
// into the codeword itself; only a small +/-IQ1S_DELTA offset is applied
// externally).
//
// GGML has no public from_float_ref for this type (an importance-matrix-
// only codebook quantizer -- see providers/ggml_internal_abi.h's
// quantize_iq1_s doc comment), so there is no quantize generator here.
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"
#include "iq_grids_data.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr float kIQ1S_DELTA = 0.125f;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kQhOffset = kQsOffset + kQK_K / 8;     // 34
constexpr int kBlockBytes = kQhOffset + kQK_K / 16;  // 50 (qh: QK_K/32 = 8 uint16 entries = QK_K/16 bytes)

class IQ1_SDequantizeGenerator : public Generator<IQ1_SDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint64_t, 1> grid(2048, "iq1s_grid");
        for (int idx = 0; idx < 2048; idx++)
            grid(idx) = iq_grids::iq1s_grid[idx];

        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;
        Expr ib = gi / 32;  // 0..7, matches qh's granularity
        Expr local = gi % 32;
        Expr l = local / 8;  // 0..3
        Expr j = local % 8;  // 0..7

        Expr qh_lo = cast<uint16_t>(blocks_(kQhOffset + ib * 2 + 0, i));
        Expr qh_hi = cast<uint16_t>(blocks_(kQhOffset + ib * 2 + 1, i));
        Expr qh_val = qh_lo | (qh_hi << 8);

        Expr dl_scale = cast<int32_t>((qh_val >> 12) & 7);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr dl = d * cast<float>(2 * dl_scale + 1);

        Expr delta = select((qh_val & 0x8000) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

        Expr qs_byte = blocks_(kQsOffset + ib * 4 + l, i);
        Expr high3 = cast<uint32_t>((qh_val >> (cast<uint16_t>(l) * 3)) & 7);
        // See iq2_s_generators.cpp's grid_idx comment: '+' instead of the
        // algebraically-equivalent '|' for reliable bounds inference, plus
        // a narrow-type cast for a hard type-level bound before the lookup.
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high3 << 8))), 0, 2047);

        Expr grid_val = grid(grid_idx);
        Expr grid_byte_bits = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr grid_signed = reinterpret<int8_t>(grid_byte_bits);

        y_(x) = dl * (cast<float>(grid_signed) + delta);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

// vec_dot(IQ1_S, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ1_SVecDotGenerator : public Generator<IQ1_SVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint64_t, 1> grid(2048, "iq1s_grid");
        for (int idx = 0; idx < 2048; idx++)
            grid(idx) = iq_grids::iq1s_grid[idx];

        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr ib = gi / 32;
            Expr local = gi % 32;
            Expr l = local / 8;
            Expr j = local % 8;

            Expr qh_lo = cast<uint16_t>(x_blocks_(kQhOffset + ib * 2 + 0, i));
            Expr qh_hi = cast<uint16_t>(x_blocks_(kQhOffset + ib * 2 + 1, i));
            Expr qh_val = qh_lo | (qh_hi << 8);

            Expr dl_scale = cast<int32_t>((qh_val >> 12) & 7);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr dl = d * cast<float>(2 * dl_scale + 1);

            Expr delta = select((qh_val & 0x8000) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

            Expr qs_byte = x_blocks_(kQsOffset + ib * 4 + l, i);
            Expr high3 = cast<uint32_t>((qh_val >> (cast<uint16_t>(l) * 3)) & 7);
            Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high3 << 8))), 0, 2047);

            Expr grid_val = grid(grid_idx);
            Expr grid_byte_bits = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
            Expr grid_signed = reinterpret<int8_t>(grid_byte_bits);

            return dl * (cast<float>(grid_signed) + delta);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ1_SDequantizeGenerator, iq1_s_dequantize)
HALIDE_REGISTER_GENERATOR(IQ1_SVecDotGenerator, iq1_s_vec_dot)
