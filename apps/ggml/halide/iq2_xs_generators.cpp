// From-scratch Halide reimplementation of GGML's IQ2_XS dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq2_xs upstream, as of GGML
// v0.15.3). Uses GGML's published iq2xs_grid/ksigns_iq2xs codebook
// constants verbatim (see iq_grids_data.h) -- fixed lookup-table data, not
// derived logic.
//
// The 74-byte block_iq2_xs layout (a 256-element superblock, 8 groups of
// 32 elements each):
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-65:  qs[32] as 32 little-endian uint16 values, one per l (4 per
//               group of 32 elements): low 9 bits index the 512-entry
//               iq2xs_grid (each entry a packed set of 8 signed bytes), top
//               7 bits index the 128-entry ksigns_iq2xs sign-pattern table
//   byte 66-73: scales[8] -- one byte per group of 32 elements, packing two
//               4-bit sub-scales (for l in {0,1} and l in {2,3})
//
// GGML has no public from_float_ref for this type either (see
// providers/ggml_internal_abi.h's quantize_iq2_xs doc comment), so there is
// no quantize generator here.
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"
#include "iq_grids_data.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kScalesOffset = kQsOffset + kQK_K / 8 * 2;  // 66
constexpr int kBlockBytes = kScalesOffset + kQK_K / 32;   // 74

class IQ2_XSDequantizeGenerator : public Generator<IQ2_XSDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint64_t, 1> grid(512, "iq2xs_grid");
        for (int idx = 0; idx < 512; idx++)
            grid(idx) = iq_grids::iq2xs_grid[idx];

        Buffer<uint8_t, 1> ksigns(128, "ksigns_iq2xs");
        for (int idx = 0; idx < 128; idx++)
            ksigns(idx) = iq_grids::ksigns_iq2xs[idx];

        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;
        Expr ib32 = gi / 32;
        Expr local = gi % 32;
        Expr l = local / 8;  // 0..3
        Expr j = local % 8;  // 0..7

        Expr qs_idx = ib32 * 4 + l;
        Expr q_lo = cast<uint16_t>(blocks_(kQsOffset + qs_idx * 2 + 0, i));
        Expr q_hi = cast<uint16_t>(blocks_(kQsOffset + qs_idx * 2 + 1, i));
        Expr qs_val = q_lo | (q_hi << 8);

        Expr grid_idx = qs_val & 511;
        Expr sign_idx = cast<uint8_t>(qs_val >> 9);

        Expr scales_byte = blocks_(kScalesOffset + ib32, i);
        Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);
        Expr db = d_decode(blocks_, kDOffset, i) * (0.5f + cast<float>(nibble)) * 0.25f;

        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr signs = ksigns(sign_idx);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j)) != 0;

        y_(x) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }

    static Expr d_decode(const Input<Buffer<uint8_t, 2>> &b, int off, Expr i) {
        Expr lo = cast<uint16_t>(b(off + 0, i));
        Expr hi = cast<uint16_t>(b(off + 1, i));
        return cast<float>(reinterpret<float16_t>(lo | (hi << 8)));
    }
};

// vec_dot(IQ2_XS, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ2_XSVecDotGenerator : public Generator<IQ2_XSVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint64_t, 1> grid(512, "iq2xs_grid");
        for (int idx = 0; idx < 512; idx++)
            grid(idx) = iq_grids::iq2xs_grid[idx];

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
            Expr j = local % 8;

            Expr qs_idx = ib32 * 4 + l;
            Expr q_lo = cast<uint16_t>(x_blocks_(kQsOffset + qs_idx * 2 + 0, i));
            Expr q_hi = cast<uint16_t>(x_blocks_(kQsOffset + qs_idx * 2 + 1, i));
            Expr qs_val = q_lo | (q_hi << 8);

            Expr grid_idx = qs_val & 511;
            Expr sign_idx = cast<uint8_t>(qs_val >> 9);

            Expr scales_byte = x_blocks_(kScalesOffset + ib32, i);
            Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);
            Expr db = IQ2_XSDequantizeGenerator::d_decode(x_blocks_, kDOffset, i) *
                      (0.5f + cast<float>(nibble)) * 0.25f;

            Expr grid_val = grid(grid_idx);
            Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
            Expr signs = ksigns(sign_idx);
            Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j)) != 0;

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

HALIDE_REGISTER_GENERATOR(IQ2_XSDequantizeGenerator, iq2_xs_dequantize)
HALIDE_REGISTER_GENERATOR(IQ2_XSVecDotGenerator, iq2_xs_vec_dot)
