// GGML's IQ3_XXS vec_dot kernel (see src/ggml-quants.c:
// dequantize_row_iq3_xxs upstream, as of GGML v0.15.3). Uses GGML's
// published iq3xxs_grid/ksigns_iq2xs codebook constants verbatim (see
// iq_grids_data.h) -- fixed lookup-table data, not derived logic.
//
// The 98-byte block_iq3_xxs layout (a 256-element superblock, 8 groups of
// 32 elements each):
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-65:  qs[64] -- grid indices, 2 bytes per l (4 l's per group of
//               32 elements) into the 256-entry iq3xxs_grid (each entry a
//               packed set of 4 signed bytes)
//   byte 66-97: "scales_and_signs"[32] -- 4 bytes per group, reassembled as
//               a little-endian uint32: top 4 bits a scale exponent, low 28
//               bits four 7-bit sign-pattern indices (into the 128-entry
//               ksigns_iq2xs table), one per l
//
// IQ3_XXS's quantize/dequantize kernels are no longer defined here -- they
// are the "iq3_xxs_quantize"/"iq3_xxs_dequantize" GENERATOR_ARGS
// instantiations of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// IQ3XXSGridDequantize, a mechanical transcription of this exact grid+sign
// lookup math into the Approximation interface; quantize still delegates
// to GGML's own reference via a Halide extern stage there too -- GGML does
// have a public from_float_ref for this type, an unweighted fallback path,
// unlike IQ2_XXS/IQ2_XS/IQ1_S, but it's still deferred since it runs the
// same kind of per-block codebook search as the other IQ/K-quant
// quantizers -- see ggml_extern_quantize.cpp for why). Only vec_dot, which
// still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"
#include "iq_grids_data.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kScalesSignsOffset = kQsOffset + kQK_K / 4;    // 66
constexpr int kBlockBytes = kScalesSignsOffset + kQK_K / 8;  // 98

// vec_dot(IQ3_XXS, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ3_XXSVecDotGenerator : public Generator<IQ3_XXSVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint32_t, 1> grid(256, "iq3xxs_grid");
        for (int idx = 0; idx < 256; idx++)
            grid(idx) = iq_grids::iq3xxs_grid[idx];

        Buffer<uint8_t, 1> ksigns(128, "ksigns_iq2xs");
        for (int idx = 0; idx < 128; idx++)
            ksigns(idx) = iq_grids::ksigns_iq2xs[idx];

        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr ib32 = gi / 32;
            Expr local = gi % 32;
            Expr l = local / 8;
            Expr j8 = local % 8;
            Expr j4 = j8 % 4;
            Expr half = j8 / 4;

            Expr grid_qs_idx = ib32 * 8 + l * 2 + half;
            Expr grid_idx = x_blocks_(kQsOffset + grid_qs_idx, i);

            Expr ss_base = kScalesSignsOffset + ib32 * 4;
            Expr b0 = cast<uint32_t>(x_blocks_(ss_base + 0, i));
            Expr b1 = cast<uint32_t>(x_blocks_(ss_base + 1, i));
            Expr b2 = cast<uint32_t>(x_blocks_(ss_base + 2, i));
            Expr b3 = cast<uint32_t>(x_blocks_(ss_base + 3, i));
            Expr aux32 = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr db = d * (0.5f + cast<float>(aux32 >> 28)) * 0.5f;

            Expr sign_idx = cast<uint8_t>((aux32 >> (cast<uint32_t>(l) * 7)) & 127);
            Expr signs = ksigns(sign_idx);

            Expr grid_val = grid(grid_idx);
            Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);
            Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j8)) != 0;

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

HALIDE_REGISTER_GENERATOR(IQ3_XXSVecDotGenerator, iq3_xxs_vec_dot)
