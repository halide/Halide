// GGML's IQ3_S vec_dot kernel (see src/ggml-quants.c: dequantize_row_iq3_s
// upstream, as of GGML v0.15.3). Uses GGML's published iq3s_grid codebook
// constants verbatim (see iq_grids_data.h) -- fixed lookup-table data, not
// derived logic.
//
// The 110-byte block_iq3_s layout (a 256-element superblock, 8 groups of
// 32 elements each, "grp" below):
//
//   byte 0-1:    fp16 delta 'd'
//   byte 2-65:   qs[64] -- 2 grid-index low-bytes per l (4 l's per group)
//   byte 66-73:  qh[8] -- one extra high bit per (l, half) pair, one byte
//                per group (bit 2l for the first grid index of l, bit 2l+1
//                for the second)
//   byte 74-105: signs[32] -- sign-pattern byte used directly as a mask
//                (like IQ2_S, no ksigns_iq2xs indirection), 4 bytes/group
//   byte 106-109: scales[4] -- one byte per PAIR of groups, two 4-bit
//                sub-scales (unlike IQ2 types, no "0.5+x*0.25" -- plain
//                odd integers 1,3,5,...,31)
//
// IQ3_S's quantize/dequantize kernels are no longer defined here -- they
// are the "iq3_s_quantize"/"iq3_s_dequantize" GENERATOR_ARGS instantiations
// of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// IQ3SGridDequantize, a mechanical transcription of this exact grid+sign
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
constexpr int kQhOffset = kQsOffset + kQK_K / 4;         // 66
constexpr int kSignsOffset = kQhOffset + kQK_K / 32;     // 74
constexpr int kScalesOffset = kSignsOffset + kQK_K / 8;  // 106
constexpr int kBlockBytes = kScalesOffset + kQK_K / 64;  // 110

// vec_dot(IQ3_S, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ3_SVecDotGenerator : public Generator<IQ3_SVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint32_t, 1> grid(512, "iq3s_grid");
        for (int idx = 0; idx < 512; idx++)
            grid(idx) = iq_grids::iq3s_grid[idx];

        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr grp = gi / 32;
            Expr local = gi % 32;
            Expr l = local / 8;
            Expr j8 = local % 8;
            Expr j4 = j8 % 4;
            Expr half = j8 / 4;

            Expr qs_byte = x_blocks_(kQsOffset + grp * 8 + l * 2 + half, i);
            Expr qh_byte = cast<uint32_t>(x_blocks_(kQhOffset + grp, i));
            Expr bit_pos = l * 2 + half;
            Expr high_bit = (qh_byte >> cast<uint32_t>(bit_pos)) & 1;
            Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high_bit << 8))), 0, 511);

            Expr signs_byte = x_blocks_(kSignsOffset + grp * 4 + l, i);
            Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j8)) != 0;

            Expr scales_byte = x_blocks_(kScalesOffset + grp / 2, i);
            Expr nibble = select((grp % 2) == 0, scales_byte & 0x0f, scales_byte >> 4);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr db = d * (1.0f + 2.0f * cast<float>(nibble));

            Expr grid_val = grid(grid_idx);
            Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);

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

HALIDE_REGISTER_GENERATOR(IQ3_SVecDotGenerator, iq3_s_vec_dot)
