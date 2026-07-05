// From-scratch Halide reimplementation of GGML's IQ3_S dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq3_s upstream, as of GGML
// v0.15.3). Uses GGML's published iq3s_grid codebook constants verbatim
// (see iq_grids_data.h) -- fixed lookup-table data, not derived logic.
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
// GGML has a public from_float_ref for this type; quantize is still
// deferred to the extern-stage scaffolding (see ggml_extern_quantize.cpp)
// since it runs the same kind of per-block codebook search as the other
// IQ/K-quant quantizers.
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

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

class IQ3_SDequantizeGenerator : public Generator<IQ3_SDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint32_t, 1> grid(512, "iq3s_grid");
        for (int idx = 0; idx < 512; idx++)
            grid(idx) = iq_grids::iq3s_grid[idx];

        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;
        Expr grp = gi / 32;  // 0..7
        Expr local = gi % 32;
        Expr l = local / 8;   // 0..3
        Expr j8 = local % 8;  // 0..7
        Expr j4 = j8 % 4;     // byte within the 4-byte grid entry
        Expr half = j8 / 4;   // 0 (first grid index of this l) or 1 (second)

        Expr qs_byte = blocks_(kQsOffset + grp * 8 + l * 2 + half, i);
        Expr qh_byte = cast<uint32_t>(blocks_(kQhOffset + grp, i));
        Expr bit_pos = l * 2 + half;
        Expr high_bit = (qh_byte >> cast<uint32_t>(bit_pos)) & 1;
        // See iq2_s_generators.cpp's grid_idx comment: '+' instead of the
        // algebraically-equivalent '|' for reliable bounds inference, plus
        // a narrow-type cast for a hard type-level bound before the lookup.
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high_bit << 8))), 0, 511);

        Expr signs_byte = blocks_(kSignsOffset + grp * 4 + l, i);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j8)) != 0;

        Expr scales_byte = blocks_(kScalesOffset + grp / 2, i);
        Expr nibble = select((grp % 2) == 0, scales_byte & 0x0f, scales_byte >> 4);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr db = d * (1.0f + 2.0f * cast<float>(nibble));

        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);

        y_(x) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class IQ3_SQuantizeGenerator : public Generator<IQ3_SQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("iq3_s_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ3_SDequantizeGenerator, iq3_s_dequantize)
HALIDE_REGISTER_GENERATOR(IQ3_SQuantizeGenerator, iq3_s_quantize)
