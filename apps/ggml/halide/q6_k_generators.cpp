// From-scratch Halide reimplementation of GGML's Q6_K dequantize kernel (see
// src/ggml-quants.c: dequantize_row_q6_K upstream, as of GGML v0.15.3). No
// GGML headers are used by the dequantize generator -- it encodes its own
// understanding of the 210-byte block_q6_K layout (a 256-element superblock,
// 16 sub-groups of 16 elements each):
//
//   byte 0-127:   ql[128] -- low 4 bits of every value (2 values per byte)
//   byte 128-191: qh[64]  -- high 2 bits of every value (4 values per byte)
//   byte 192-207: scales[16] -- one SIGNED int8 scale per sub-group
//   byte 208-209: fp16 delta 'd' (super-block scale)
//
// Quantize is NOT reimplemented here: GGML's reference quantizer runs an
// iterative per-sub-block error-minimizing scale search (make_qx_quants in
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
constexpr int kQlOffset = 0;
constexpr int kQhOffset = kQK_K / 2;                  // 128
constexpr int kScalesOffset = kQhOffset + kQK_K / 4;  // 192
constexpr int kDOffset = kScalesOffset + kQK_K / 16;  // 208
constexpr int kBlockBytes = kDOffset + 2;             // 210

class Q6_KDequantizeGenerator : public Generator<Q6_KDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr gi = x % kQK_K;  // element index within the superblock, [0, 256)
        Expr i = x / kQK_K;

        // GGML processes the superblock as two 128-element halves; within
        // each half, 4 "groups" of 32 elements (offsets +0/+32/+64/+96),
        // where group parity (0/1) selects the qs nibble index (l vs l+32)
        // and group/2 selects low vs high nibble of ql; qh's 2-bit field
        // index equals the group number, and the scale index combines the
        // group number with is=l/16 (which half of the 32-element group).
        Expr half = gi / 128;
        Expr local = gi % 128;
        Expr group = local / 32;  // 0..3
        Expr l = local % 32;      // 0..31
        Expr is = l / 16;         // 0..1

        Expr ql_extra = select((group % 2) == 1, 32, 0);
        Expr ql_abs = half * 64 + l + ql_extra;
        Expr ql_byte = blocks_(kQlOffset + ql_abs, i);
        Expr ql_nibble = select(group < 2, cast<int32_t>(ql_byte & 0x0f), cast<int32_t>(ql_byte >> 4));

        Expr qh_shift = group * 2;
        Expr qh_abs = half * 32 + l;
        Expr qh_byte = blocks_(kQhOffset + qh_abs, i);
        Expr high2 = cast<int32_t>((qh_byte >> qh_shift) & 3);

        Expr q_signed = (ql_nibble | (high2 << 4)) - 32;

        Expr sc_abs = half * 8 + is + group * 2;
        Expr sc = reinterpret<int8_t>(blocks_(kScalesOffset + sc_abs, i));

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = d * cast<float>(sc) * cast<float>(q_signed);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q6_KQuantizeGenerator : public Generator<Q6_KQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("q6_k_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q6_KDequantizeGenerator, q6_k_dequantize)
HALIDE_REGISTER_GENERATOR(Q6_KQuantizeGenerator, q6_k_quantize)
