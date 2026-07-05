// From-scratch Halide reimplementation of GGML's Q2_K dequantize kernel (see
// src/ggml-quants.c: dequantize_row_q2_K upstream, as of GGML v0.15.3). No
// GGML headers are used by the dequantize generator -- it encodes its own
// understanding of the 84-byte block_q2_K layout (a 256-element superblock,
// 16 sub-groups of 16 elements each):
//
//   byte 0-15:  scales[16] -- one byte per sub-group, low nibble = 4-bit
//               scale, high nibble = 4-bit min-scale
//   byte 16-79: qs[64] -- 2 bits per element (4 elements per byte)
//   byte 80-81: fp16 delta 'd' (super-block scale for the scales)
//   byte 82-83: fp16 'dmin' (super-block scale for the mins)
//
// Quantize is NOT reimplemented here: GGML's reference quantizer runs an
// iterative per-sub-block error-minimizing scale search (make_qkx2_quants in
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
constexpr int kNumGroups = kQK_K / 16;                   // 16
constexpr int kBlockBytes = kNumGroups + kQK_K / 4 + 4;  // 16 + 64 + 4 = 84
constexpr int kScalesOffset = 0;
constexpr int kQsOffset = kNumGroups;            // 16
constexpr int kDOffset = kQsOffset + kQK_K / 4;  // 80
constexpr int kDminOffset = kDOffset + 2;        // 82

class Q2_KDequantizeGenerator : public Generator<Q2_KDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr gi = x % kQK_K;  // element index within the superblock, [0, 256)
        Expr i = x / kQK_K;

        // GGML processes the superblock as two 128-element halves; within
        // each half, 4 "shift groups" of 32 elements each, where the j-th
        // shift group reads the same 32 qs bytes as every other group in
        // that half but extracts a different 2-bit field (bits 2*j, 2*j+1)
        // and uses a different scale byte for the first/second 16 of the 32.
        Expr half = gi / 128;
        Expr local = gi % 128;
        Expr j = local / 32;      // which shift group, 0..3
        Expr rem32 = local % 32;  // position within this half's 32-byte qs window
        Expr shift = j * 2;

        Expr q_byte_idx = half * 32 + rem32;
        Expr scale_idx = half * 8 + j * 2 + select(rem32 >= 16, 1, 0);

        Expr q_byte = blocks_(kQsOffset + q_byte_idx, i);
        Expr sc = blocks_(kScalesOffset + scale_idx, i);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr m_lo = cast<uint16_t>(blocks_(kDminOffset + 0, i));
        Expr m_hi = cast<uint16_t>(blocks_(kDminOffset + 1, i));
        Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));

        Expr dl = d * cast<float>(sc & 0x0f);
        Expr ml = m * cast<float>(cast<uint32_t>(sc) >> 4);

        Expr twobit = cast<int32_t>((q_byte >> shift) & 3);

        y_(x) = dl * cast<float>(twobit) - ml;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q2_KQuantizeGenerator : public Generator<Q2_KQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("q2_k_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q2_KDequantizeGenerator, q2_k_dequantize)
HALIDE_REGISTER_GENERATOR(Q2_KQuantizeGenerator, q2_k_quantize)
