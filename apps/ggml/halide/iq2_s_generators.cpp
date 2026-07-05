// From-scratch Halide reimplementation of GGML's IQ2_S dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq2_s upstream, as of GGML
// v0.15.3). Uses GGML's published iq2s_grid codebook constants verbatim
// (see iq_grids_data.h) -- fixed lookup-table data, not derived logic.
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
constexpr int kSignsOffset = kQsOffset + kQK_K / 8;      // 34
constexpr int kQhOffset = kSignsOffset + kQK_K / 8;      // 66
constexpr int kScalesOffset = kQhOffset + kQK_K / 32;    // 74
constexpr int kBlockBytes = kScalesOffset + kQK_K / 32;  // 82

class IQ2_SDequantizeGenerator : public Generator<IQ2_SDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint64_t, 1> grid(1024, "iq2s_grid");
        for (int idx = 0; idx < 1024; idx++)
            grid(idx) = iq_grids::iq2s_grid[idx];

        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;
        Expr ib32 = gi / 32;
        Expr local = gi % 32;
        Expr l = local / 8;  // 0..3
        Expr j = local % 8;  // 0..7

        Expr qs_l = blocks_(kQsOffset + ib32 * 4 + l, i);
        Expr qh_byte = cast<uint32_t>(blocks_(kQhOffset + ib32, i));
        // A *variable-amount* left shift feeding a Buffer index defeats
        // Halide's bounds inference even when masked afterward (unlike a
        // constant-amount shift, or a variable-amount *right* shift) --
        // branch over l's 4 possible values so each arm's shift amount is a
        // compile-time constant Halide can analyze normally.
        Expr extra_bits = select(l == 0, (qh_byte << 8) & 0x300,
                                 l == 1, (qh_byte << 6) & 0x300,
                                 l == 2, (qh_byte << 4) & 0x300,
                                 (qh_byte << 2) & 0x300);
        // '+' instead of the algebraically-equivalent '|' (the two operands
        // never share a set bit: qs_l < 256 uses bits 0-7, extra_bits only
        // ever sets bit 8 or 9) -- Halide's interval arithmetic for '+'
        // propagates a tight bound more reliably than for '|'.
        Expr grid_idx_raw = cast<uint32_t>(qs_l) + extra_bits;
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(grid_idx_raw)), 0, 1023);

        Expr signs_byte = blocks_(kSignsOffset + ib32 * 4 + l, i);

        Expr scales_byte = blocks_(kScalesOffset + ib32, i);
        Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr db = d * (0.5f + cast<float>(nibble)) * 0.25f;

        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j)) != 0;

        y_(x) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class IQ2_SQuantizeGenerator : public Generator<IQ2_SQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("iq2_s_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ2_SDequantizeGenerator, iq2_s_dequantize)
HALIDE_REGISTER_GENERATOR(IQ2_SQuantizeGenerator, iq2_s_quantize)
