// From-scratch Halide reimplementation of GGML's Q3_K dequantize kernel (see
// src/ggml-quants.c: dequantize_row_q3_K upstream, as of GGML v0.15.3). No
// GGML headers are used by the dequantize generator -- it encodes its own
// understanding of the 110-byte block_q3_K layout (a 256-element
// superblock, 16 sub-groups of 16 elements each):
//
//   byte 0-31:   hmask[32] -- the 3rd (high) bit of every value, 1 bit each;
//                bit position (0..7) rotates once per (half, shift-group)
//                pair across the whole superblock (32 bytes * 8 bits = 256)
//   byte 32-95:  qs[64] -- low 2 bits of every value (4 elements per byte)
//   byte 96-107: scales[12] -- 16 SIGNED 6-bit values, bit-interleaved
//                across 12 bytes (a different packing from Q4_K/Q5_K's
//                get_scale_min_k4 -- see the derivation below)
//   byte 108-109: fp16 delta 'd' (super-block scale)
//
// GGML's reference unpacks the 16 signed 6-bit scales via a 4-uint32
// "aux[]" bit-shuffle (see the real dequantize_row_q3_K). Re-deriving it
// byte-by-byte for scale index s (0..15) collapses to a uniform formula:
// the 2 high bits always live in byte (s%4)+8, at bit-shift 2*(s/4); the 4
// low bits live in byte (s%8), taken from the byte's low nibble if s<8 or
// high nibble if s>=8. The final signed value is (low|(high<<4)) - 32.
//
// Quantize is NOT reimplemented here: GGML's reference quantizer runs an
// iterative per-sub-block error-minimizing scale search (make_q3_quants in
// src/ggml-quants.c), which is deferred -- see ggml_extern_quantize.cpp for
// why and how this Func instead calls out to GGML's own reference via a
// Halide extern stage, as scaffolding for a from-scratch search later.
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kHmaskOffset = 0;
constexpr int kQsOffset = kQK_K / 8;                  // 32
constexpr int kScalesOffset = kQsOffset + kQK_K / 4;  // 96
constexpr int kDOffset = kScalesOffset + 12;          // 108
constexpr int kBlockBytes = kDOffset + 2;             // 110

class Q3_KDequantizeGenerator : public Generator<Q3_KDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr gi = x % kQK_K;  // element index within the superblock, [0, 256)
        Expr i = x / kQK_K;

        // Same (half, shift-group, 32-byte window) structure as Q2_K.
        Expr half = gi / 128;
        Expr local = gi % 128;
        Expr j = local / 32;      // shift group, 0..3
        Expr rem32 = local % 32;  // position within this half's 32-byte qs window
        Expr shift = j * 2;
        Expr bit_pos = half * 4 + j;  // rotating hmask bit, 0..7

        Expr q_byte_idx = half * 32 + rem32;
        Expr scale_idx = half * 8 + j * 2 + select(rem32 >= 16, 1, 0);

        Expr q_byte = blocks_(kQsOffset + q_byte_idx, i);
        Expr twobit = cast<int32_t>((q_byte >> shift) & 3);

        Expr hm_byte = blocks_(kHmaskOffset + rem32, i);
        Expr high_set = ((cast<uint32_t>(hm_byte) >> bit_pos) & 1) != 0;
        Expr offset4 = select(high_set, 0, 4);

        // 16 signed 6-bit scales, bit-interleaved across 12 bytes (see file
        // comment above for the derivation of this uniform formula).
        Expr low_byte_idx = scale_idx % 8;
        Expr use_high_nibble = scale_idx >= 8;
        Expr scale_low_byte = blocks_(kScalesOffset + low_byte_idx, i);
        Expr low_val = select(use_high_nibble,
                              cast<int32_t>(scale_low_byte >> 4),
                              cast<int32_t>(scale_low_byte & 0x0f));
        Expr high2_byte_idx = (scale_idx % 4) + 8;
        Expr high2_shift = (scale_idx / 4) * 2;
        Expr high2 = cast<int32_t>((blocks_(kScalesOffset + high2_byte_idx, i) >> high2_shift) & 0x3);
        Expr scale_signed = (low_val | (high2 << 4)) - 32;

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr dl = d * cast<float>(scale_signed);

        y_(x) = dl * cast<float>(twobit - offset4);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q3_KQuantizeGenerator : public Generator<Q3_KQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("q3_k_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q3_KDequantizeGenerator, q3_k_dequantize)
HALIDE_REGISTER_GENERATOR(Q3_KQuantizeGenerator, q3_k_quantize)
