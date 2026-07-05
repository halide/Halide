// From-scratch Halide reimplementation of GGML's Q4_K dequantize kernel (see
// src/ggml-quants.c: dequantize_row_q4_K / get_scale_min_k4 upstream, as of
// GGML v0.15.3). No GGML headers are used by the dequantize generator -- it
// encodes its own understanding of the 144-byte block_q4_K layout (a
// 256-element superblock, 8 sub-groups of 32 elements each):
//
//   byte 0-1:    fp16 delta 'd' (super-block scale for the scales)
//   byte 2-3:    fp16 'dmin' (super-block scale for the mins)
//   byte 4-15:   scales[12] -- 8 pairs of 6-bit (scale, min) values, packed
//                via GGML's get_scale_min_k4 scheme (see below)
//   byte 16-143: qs[128] -- 4 bits per element (2 elements per byte)
//
// get_scale_min_k4(j, q) for j in [0,8): for j<4, the scale/min are simply
// the low 6 bits of q[j] / q[j+4]; for j>=4, each is a 4-bit low part from
// q[j+4] combined with a 2-bit high part borrowed from the top 2 bits of an
// earlier byte (q[j-4] for the scale, q[j] for the min) -- a bit-interleaved
// packing that lets 8 six-bit values fit in 6 bytes' worth of budget instead
// of 8 (6*8=48 bits vs 12 bytes = 96 bits budgeted, actually using the 4
// extra bytes' top 2 bits per pair to store the second half's high bits).
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
constexpr int kDOffset = 0;
constexpr int kDminOffset = 2;
constexpr int kScalesOffset = 4;
constexpr int kQsOffset = kScalesOffset + 12;       // 16
constexpr int kBlockBytes = kQsOffset + kQK_K / 2;  // 144

class Q4_KDequantizeGenerator : public Generator<Q4_KDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr gi = x % kQK_K;  // element index within the superblock, [0, 256)
        Expr i = x / kQK_K;

        // GGML processes the superblock in 4 iterations of 64 elements;
        // each iteration reuses the same 32 qs bytes for its first 32
        // elements (low nibble) and second 32 elements (high nibble), with
        // a distinct (scale, min) pair per half.
        Expr iter = gi / 64;  // 0..3
        Expr local64 = gi % 64;
        Expr half64 = local64 / 32;  // 0 (low nibble) or 1 (high nibble)
        Expr l = local64 % 32;       // 0..31

        Expr scale_idx = iter * 2 + half64;  // 0..7

        Expr qs_byte = blocks_(kQsOffset + iter * 32 + l, i);
        Expr nibble = select(half64 == 0, cast<int32_t>(qs_byte & 0x0f), cast<int32_t>(qs_byte >> 4));

        // get_scale_min_k4(scale_idx, scales): 8 pairs of 6-bit (scale, min)
        // values bit-interleaved across 12 bytes (see file comment above).
        Expr jj = clamp(scale_idx - 4, 0, 3);
        Expr sc = select(scale_idx < 4,
                         blocks_(kScalesOffset + scale_idx, i) & 0x3f,
                         cast<uint8_t>((blocks_(kScalesOffset + 8 + jj, i) & 0x0f) | ((blocks_(kScalesOffset + jj, i) >> 6) << 4)));
        Expr m = select(scale_idx < 4,
                        blocks_(kScalesOffset + scale_idx + 4, i) & 0x3f,
                        cast<uint8_t>((blocks_(kScalesOffset + 8 + jj, i) >> 4) | ((blocks_(kScalesOffset + 4 + jj, i) >> 6) << 4)));

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr dmin_lo = cast<uint16_t>(blocks_(kDminOffset + 0, i));
        Expr dmin_hi = cast<uint16_t>(blocks_(kDminOffset + 1, i));
        Expr dmin = cast<float>(reinterpret<float16_t>(dmin_lo | (dmin_hi << 8)));

        Expr d1 = d * cast<float>(sc);
        Expr m1 = dmin * cast<float>(m);

        y_(x) = d1 * cast<float>(nibble) - m1;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q4_KQuantizeGenerator : public Generator<Q4_KQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("q4_k_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q4_KDequantizeGenerator, q4_k_dequantize)
HALIDE_REGISTER_GENERATOR(Q4_KQuantizeGenerator, q4_k_quantize)
