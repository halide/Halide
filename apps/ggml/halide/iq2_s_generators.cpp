// GGML's IQ2_S vec_dot kernel (see src/ggml-quants.c: dequantize_row_iq2_s
// upstream, as of GGML v0.15.3). Uses GGML's published iq2s_grid codebook
// constants verbatim (see iq_grids_data.h) -- fixed lookup-table data, not
// derived logic.
//
// The 82-byte block_iq2_s layout (a 256-element superblock, 8 groups of 32
// elements each):
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-33:  qs[32] -- one grid-index low-byte per l (4 l's per group)
//   byte 34-65: signs[32] -- the sign-pattern byte used DIRECTLY as a mask
//               (unlike IQ2_XXS/IQ2_XS, no ksigns_iq2xs indirection here)
//   byte 66-73: qh[8] -- 2 extra high bits per l, one byte per group of 32
//   byte 74-81: scales[8] -- one byte per group, two 4-bit sub-scales
//
// IQ2_S's quantize/dequantize kernels are no longer defined here -- they
// are the "iq2_s_quantize"/"iq2_s_dequantize" GENERATOR_ARGS instantiations
// of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// IQ2SGridDequantize, a mechanical transcription of this exact grid+sign
// lookup math into the Approximation interface; quantize still delegates
// to GGML's own reference via a Halide extern stage there too, since GGML's
// reference quantizer runs the same kind of per-block codebook search as
// the other IQ/K-quant quantizers -- see ggml_extern_quantize.cpp for why).
// Only vec_dot, which still hand-rolls its own dequantize math, is
// unscheduled beyond the minimum Halide requires for legality -- scheduling
// for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"
#include "iq_grids_data.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kSignsOffset = kQsOffset + kQK_K / 8;      // 34
constexpr int kQhOffset = kSignsOffset + kQK_K / 8;      // 66
constexpr int kScalesOffset = kQhOffset + kQK_K / 32;    // 74
constexpr int kBlockBytes = kScalesOffset + kQK_K / 32;  // 82

// vec_dot(IQ2_S, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ2_SVecDotGenerator : public Generator<IQ2_SVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint64_t, 1> grid(1024, "iq2s_grid");
        for (int idx = 0; idx < 1024; idx++)
            grid(idx) = iq_grids::iq2s_grid[idx];

        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr ib32 = gi / 32;
            Expr local = gi % 32;
            Expr l = local / 8;
            Expr j = local % 8;

            Expr qs_l = x_blocks_(kQsOffset + ib32 * 4 + l, i);
            Expr qh_byte = cast<uint32_t>(x_blocks_(kQhOffset + ib32, i));
            Expr extra_bits = select(l == 0, (qh_byte << 8) & 0x300,
                                     l == 1, (qh_byte << 6) & 0x300,
                                     l == 2, (qh_byte << 4) & 0x300,
                                     (qh_byte << 2) & 0x300);
            Expr grid_idx_raw = cast<uint32_t>(qs_l) + extra_bits;
            Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(grid_idx_raw)), 0, 1023);

            Expr signs_byte = x_blocks_(kSignsOffset + ib32 * 4 + l, i);

            Expr scales_byte = x_blocks_(kScalesOffset + ib32, i);
            Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr db = d * (0.5f + cast<float>(nibble)) * 0.25f;

            Expr grid_val = grid(grid_idx);
            Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
            Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j)) != 0;

            return db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ2_SVecDotGenerator, iq2_s_vec_dot)
