// From-scratch Halide reimplementation of GGML's IQ2_XXS dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq2_xxs upstream, as of GGML
// v0.15.3). Uses GGML's published iq2xxs_grid/ksigns_iq2xs codebook
// constants verbatim (see iq_grids_data.h) -- these are fixed lookup-table
// data, not derived logic, so embedding them isn't a GGML dependency in the
// sense the rest of this directory avoids (no GGML headers/symbols).
//
// The 66-byte block_iq2_xxs layout (a 256-element superblock, 8 groups of
// 32 elements each):
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-65:  qs[32] as 16 uint16 pairs -- per group of 32 elements (8
//               bytes = 2 uint32 words "aux32[0]", "aux32[1]"): aux32[0]'s
//               4 raw bytes are 4 grid indices (one per l in [0,4)) into the
//               256-entry iq2xxs_grid (each entry a packed set of 8 signed
//               bytes); aux32[1] packs a 4-bit-ish scale exponent in its top
//               4 bits and a 7-bit sign-pattern index (into the 128-entry
//               ksigns_iq2xs table) for each l in its low 28 bits
//
// GGML has no public from_float_ref for this type (an importance-matrix-
// aware quantizer only), so there is no quantize generator here at all.
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
constexpr int kBlockBytes = kQsOffset + kQK_K / 8 * 2;  // 66

class IQ2_XXSDequantizeGenerator : public Generator<IQ2_XXSDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint64_t, 1> grid(256, "iq2xxs_grid");
        for (int idx = 0; idx < 256; idx++)
            grid(idx) = iq_grids::iq2xxs_grid[idx];

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

        // aux32[0]'s bytes are the raw qs bytes directly (byte l of the
        // first 4-byte half of this group's 8-byte window).
        Expr grid_idx = blocks_(kQsOffset + ib32 * 8 + l, i);

        // aux32[1]: the second 4-byte half, reassembled little-endian.
        Expr a1b0 = cast<uint32_t>(blocks_(kQsOffset + ib32 * 8 + 4, i));
        Expr a1b1 = cast<uint32_t>(blocks_(kQsOffset + ib32 * 8 + 5, i));
        Expr a1b2 = cast<uint32_t>(blocks_(kQsOffset + ib32 * 8 + 6, i));
        Expr a1b3 = cast<uint32_t>(blocks_(kQsOffset + ib32 * 8 + 7, i));
        Expr aux32_1 = a1b0 | (a1b1 << 8) | (a1b2 << 16) | (a1b3 << 24);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr db = d * (0.5f + cast<float>(aux32_1 >> 28)) * 0.25f;

        Expr sign_idx = cast<uint8_t>((aux32_1 >> (cast<uint32_t>(l) * 7)) & 127);
        Expr signs = ksigns(sign_idx);

        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j)) != 0;

        y_(x) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ2_XXSDequantizeGenerator, iq2_xxs_dequantize)
