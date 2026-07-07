// GGML's Q3_K vec_dot kernel (see src/ggml-quants.c: dequantize_row_q3_K
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 110-byte block_q3_K layout (a
// 256-element superblock, 16 sub-groups of 16 elements each):
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
// Q3_K's quantize/dequantize kernels are no longer defined here -- they are
// the "q3_k_quantize"/"q3_k_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based k_quant_quantize/k_quant_dequantize
// generators in k_quant_generators.cpp (see quant_components.h's
// KQuantDequantize/Q3KScalePack/CombinedBitsCode/TwoBitPack/RotatingBitPack;
// quantize still delegates to GGML's own reference via a Halide extern
// stage there too, since GGML's reference quantizer runs an iterative
// per-sub-block error-minimizing scale search -- make_q3_quants in
// src/ggml-quants.c -- see ggml_extern_quantize.cpp for why). Only vec_dot,
// which still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kHmaskOffset = 0;
constexpr int kQsOffset = kQK_K / 8;                  // 32
constexpr int kScalesOffset = kQsOffset + kQK_K / 4;  // 96
constexpr int kDOffset = kScalesOffset + 12;          // 108
constexpr int kBlockBytes = kDOffset + 2;             // 110

// vec_dot(Q3_K, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q3_KVecDotGenerator : public Generator<Q3_KVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr gi = x % kQK_K;
            Expr i = x / kQK_K;

            Expr half = gi / 128;
            Expr local = gi % 128;
            Expr j = local / 32;
            Expr rem32 = local % 32;
            Expr shift = j * 2;
            Expr bit_pos = half * 4 + j;

            Expr q_byte_idx = half * 32 + rem32;
            Expr scale_idx = half * 8 + j * 2 + select(rem32 >= 16, 1, 0);

            Expr q_byte = x_blocks_(kQsOffset + q_byte_idx, i);
            Expr twobit = cast<int32_t>((q_byte >> shift) & 3);

            Expr hm_byte = x_blocks_(kHmaskOffset + rem32, i);
            Expr high_set = ((cast<uint32_t>(hm_byte) >> bit_pos) & 1) != 0;
            Expr offset4 = select(high_set, 0, 4);

            Expr low_byte_idx = scale_idx % 8;
            Expr use_high_nibble = scale_idx >= 8;
            Expr scale_low_byte = x_blocks_(kScalesOffset + low_byte_idx, i);
            Expr low_val = select(use_high_nibble,
                                  cast<int32_t>(scale_low_byte >> 4),
                                  cast<int32_t>(scale_low_byte & 0x0f));
            Expr high2_byte_idx = (scale_idx % 4) + 8;
            Expr high2_shift = (scale_idx / 4) * 2;
            Expr high2 = cast<int32_t>((x_blocks_(kScalesOffset + high2_byte_idx, i) >> high2_shift) & 0x3);
            Expr scale_signed = (low_val | (high2 << 4)) - 32;

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

            Expr dl = d * cast<float>(scale_signed);

            return dl * cast<float>(twobit - offset4);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q3_KVecDotGenerator, q3_k_vec_dot)
