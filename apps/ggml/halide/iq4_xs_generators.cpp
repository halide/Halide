// GGML's IQ4_XS vec_dot kernel (see src/ggml-quants.c: dequantize_row_iq4_xs
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 136-byte block_iq4_xs layout (a
// 256-element superblock, 8 sub-blocks of 32 elements each), the
// superblock generalization of IQ4_NL's fixed 16-value codebook:
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-3:   scales_h -- a little-endian uint16, 2 bits per sub-block
//   byte 4-7:   scales_l[4] -- 4 bits per sub-block, 2 sub-blocks per byte
//   byte 8-135: qs[128] -- 4 bits per element (2 elements per byte), each a
//               codebook index into the same kvalues_iq4nl table as IQ4_NL
//
// IQ4_XS's quantize/dequantize kernels are no longer defined here -- they
// are the "iq4_xs_quantize"/"iq4_xs_dequantize" GENERATOR_ARGS
// instantiations of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// IQ4XSDequantize, a mechanical transcription of this exact two-level-scale
// codebook math into the Approximation interface; quantize still delegates
// to GGML's own reference via a Halide extern stage there too, since GGML's
// reference quantizer runs a per-sub-block error-minimizing search over
// that codebook -- see ggml_extern_quantize.cpp for why). Only vec_dot,
// which still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kScalesHOffset = 2;
constexpr int kScalesLOffset = 4;
constexpr int kQsOffset = 8;
constexpr int kBlockBytes = kQsOffset + kQK_K / 2;  // 136

// kvalues_iq4nl = {-127, -104, -83, -65, -49, -35, -22, -10,
//                     1,   13,  25,  38,  53,  69,  89, 113}
Expr lookup_iq4nl(Expr idx) {
    return select(idx == 0, -127, idx == 1, -104, idx == 2, -83, idx == 3, -65,
                  idx == 4, -49, idx == 5, -35, idx == 6, -22, idx == 7, -10,
                  idx == 8, 1, idx == 9, 13, idx == 10, 25, idx == 11, 38,
                  idx == 12, 53, idx == 13, 69, idx == 14, 89, 113);
}

// vec_dot(IQ4_XS, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ4_XSVecDotGenerator : public Generator<IQ4_XSVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr ib = gi / 32;
            Expr local = gi % 32;
            Expr j = local % 16;
            Expr is_low = local < 16;
            Expr qs_byte_idx = ib * 16 + j;
            Expr byte = x_blocks_(kQsOffset + qs_byte_idx, i);
            Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));
            Expr val = lookup_iq4nl(nibble);
            Expr scales_l_byte = x_blocks_(kScalesLOffset + ib / 2, i);
            Expr low4 = cast<int32_t>((scales_l_byte >> ((ib % 2) * 4)) & 0x0f);
            Expr sh_lo = cast<uint16_t>(x_blocks_(kScalesHOffset + 0, i));
            Expr sh_hi = cast<uint16_t>(x_blocks_(kScalesHOffset + 1, i));
            Expr scales_h = sh_lo | (sh_hi << 8);
            Expr high2 = cast<int32_t>((scales_h >> cast<uint16_t>(ib * 2)) & 3);
            Expr ls = low4 | (high2 << 4);
            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr dl = d * cast<float>(ls - 32);
            return dl * cast<float>(val);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ4_XSVecDotGenerator, iq4_xs_vec_dot)
