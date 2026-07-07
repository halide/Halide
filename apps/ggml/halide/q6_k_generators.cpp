// GGML's Q6_K vec_dot kernel (see src/ggml-quants.c: dequantize_row_q6_K
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 210-byte block_q6_K layout (a
// 256-element superblock, 16 sub-groups of 16 elements each):
//
//   byte 0-127:   ql[128] -- low 4 bits of every value (2 values per byte)
//   byte 128-191: qh[64]  -- high 2 bits of every value (4 values per byte)
//   byte 192-207: scales[16] -- one SIGNED int8 scale per sub-group
//   byte 208-209: fp16 delta 'd' (super-block scale)
//
// Q6_K's quantize/dequantize kernels are no longer defined here -- they are
// the "q6_k_quantize"/"q6_k_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based k_quant_quantize/k_quant_dequantize
// generators in k_quant_generators.cpp (see quant_components.h's
// KQuantDequantize/CombinedBitsCode/PlanarBitPack/BytePack;
// quantize still delegates to GGML's own reference via a Halide extern
// stage there too, since GGML's reference quantizer runs an iterative
// per-sub-block error-minimizing scale search -- make_qx_quants in
// src/ggml-quants.c -- see ggml_extern_quantize.cpp for why). Only vec_dot,
// which still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kQlOffset = 0;
constexpr int kQhOffset = kQK_K / 2;                  // 128
constexpr int kScalesOffset = kQhOffset + kQK_K / 4;  // 192
constexpr int kDOffset = kScalesOffset + kQK_K / 16;  // 208
constexpr int kBlockBytes = kDOffset + 2;             // 210

// vec_dot(Q6_K, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q6_KVecDotGenerator : public Generator<Q6_KVecDotGenerator> {
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
            Expr group = local / 32;
            Expr l = local % 32;
            Expr is = l / 16;

            Expr ql_extra = select((group % 2) == 1, 32, 0);
            Expr ql_abs = half * 64 + l + ql_extra;
            Expr ql_byte = x_blocks_(kQlOffset + ql_abs, i);
            Expr ql_nibble = select(group < 2, cast<int32_t>(ql_byte & 0x0f), cast<int32_t>(ql_byte >> 4));

            Expr qh_shift = group * 2;
            Expr qh_abs = half * 32 + l;
            Expr qh_byte = x_blocks_(kQhOffset + qh_abs, i);
            Expr high2 = cast<int32_t>((qh_byte >> qh_shift) & 3);

            Expr q_signed = (ql_nibble | (high2 << 4)) - 32;

            Expr sc_abs = half * 8 + is + group * 2;
            Expr sc = reinterpret<int8_t>(x_blocks_(kScalesOffset + sc_abs, i));

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

            return d * cast<float>(sc) * cast<float>(q_signed);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q6_KVecDotGenerator, q6_k_vec_dot)
