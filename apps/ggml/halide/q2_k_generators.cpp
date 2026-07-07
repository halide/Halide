// GGML's Q2_K vec_dot kernel (see src/ggml-quants.c: dequantize_row_q2_K
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 84-byte block_q2_K layout (a
// 256-element superblock, 16 sub-groups of 16 elements each):
//
//   byte 0-15:  scales[16] -- one byte per sub-group, low nibble = 4-bit
//               scale, high nibble = 4-bit min-scale
//   byte 16-79: qs[64] -- 2 bits per element (4 elements per byte)
//   byte 80-81: fp16 delta 'd' (super-block scale for the scales)
//   byte 82-83: fp16 'dmin' (super-block scale for the mins)
//
// Q2_K's quantize/dequantize kernels are no longer defined here -- they are
// the "q2_k_quantize"/"q2_k_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based k_quant_quantize/k_quant_dequantize
// generators in k_quant_generators.cpp (see quant_components.h's
// KQuantDequantize/NibblePairPack/PlanarBitPack; quantize still delegates to
// GGML's own reference via a Halide extern stage there too, since GGML's
// reference quantizer runs an iterative per-sub-block error-minimizing
// scale search -- make_qkx2_quants in src/ggml-quants.c -- see
// ggml_extern_quantize.cpp for why). Only vec_dot, which still hand-rolls
// its own dequantize math, is unscheduled beyond the minimum Halide
// requires for legality -- scheduling for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kNumGroups = kQK_K / 16;                   // 16
constexpr int kBlockBytes = kNumGroups + kQK_K / 4 + 4;  // 16 + 64 + 4 = 84
constexpr int kScalesOffset = 0;
constexpr int kQsOffset = kNumGroups;            // 16
constexpr int kDOffset = kQsOffset + kQK_K / 4;  // 80
constexpr int kDminOffset = kDOffset + 2;        // 82

// vec_dot(Q2_K, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q2_KVecDotGenerator : public Generator<Q2_KVecDotGenerator> {
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

            Expr q_byte_idx = half * 32 + rem32;
            Expr scale_idx = half * 8 + j * 2 + select(rem32 >= 16, 1, 0);

            Expr q_byte = x_blocks_(kQsOffset + q_byte_idx, i);
            Expr sc = x_blocks_(kScalesOffset + scale_idx, i);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

            Expr m_lo = cast<uint16_t>(x_blocks_(kDminOffset + 0, i));
            Expr m_hi = cast<uint16_t>(x_blocks_(kDminOffset + 1, i));
            Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));

            Expr dl = d * cast<float>(sc & 0x0f);
            Expr ml = m * cast<float>(cast<uint32_t>(sc) >> 4);

            Expr twobit = cast<int32_t>((q_byte >> shift) & 3);

            return dl * cast<float>(twobit) - ml;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q2_KVecDotGenerator, q2_k_vec_dot)
