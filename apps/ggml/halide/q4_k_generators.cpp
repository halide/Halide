// GGML's Q4_K vec_dot kernel (see src/ggml-quants.c: dequantize_row_q4_K /
// get_scale_min_k4 upstream, as of GGML v0.15.3). No GGML headers are used
// here -- this file encodes its own understanding of the 144-byte
// block_q4_K layout (a 256-element superblock, 8 sub-groups of 32 elements
// each):
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
// Q4_K's quantize/dequantize kernels are no longer defined here -- they are
// the "q4_k_quantize"/"q4_k_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based k_quant_quantize/k_quant_dequantize
// generators in k_quant_generators.cpp (see quant_components.h's
// KQuantDequantize/K4ScaleMinPack/SubBlockNibblePack, which reproduce this
// exact bit-interleaved scale/min packing and nibble layout; quantize still
// delegates to GGML's own reference via a Halide extern stage there too,
// since GGML's reference quantizer runs an iterative per-sub-block
// error-minimizing scale search -- make_qkx2_quants in src/ggml-quants.c --
// see ggml_extern_quantize.cpp for why). Only vec_dot, which still
// hand-rolls its own dequantize math, is unscheduled beyond the minimum
// Halide requires for legality -- scheduling for performance is a later
// step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kDminOffset = 2;
constexpr int kScalesOffset = 4;
constexpr int kQsOffset = kScalesOffset + 12;       // 16
constexpr int kBlockBytes = kQsOffset + kQK_K / 2;  // 144

// vec_dot(Q4_K, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q4_KVecDotGenerator : public Generator<Q4_KVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr gi = x % kQK_K;
            Expr i = x / kQK_K;

            Expr iter = gi / 64;
            Expr local64 = gi % 64;
            Expr half64 = local64 / 32;
            Expr l = local64 % 32;

            Expr scale_idx = iter * 2 + half64;

            Expr qs_byte = x_blocks_(kQsOffset + iter * 32 + l, i);
            Expr nibble = select(half64 == 0, cast<int32_t>(qs_byte & 0x0f), cast<int32_t>(qs_byte >> 4));

            Expr jj = clamp(scale_idx - 4, 0, 3);
            Expr sc = select(scale_idx < 4,
                             x_blocks_(kScalesOffset + scale_idx, i) & 0x3f,
                             cast<uint8_t>((x_blocks_(kScalesOffset + 8 + jj, i) & 0x0f) |
                                           ((x_blocks_(kScalesOffset + jj, i) >> 6) << 4)));
            Expr m = select(scale_idx < 4,
                            x_blocks_(kScalesOffset + scale_idx + 4, i) & 0x3f,
                            cast<uint8_t>((x_blocks_(kScalesOffset + 8 + jj, i) >> 4) |
                                          ((x_blocks_(kScalesOffset + 4 + jj, i) >> 6) << 4)));

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

            Expr dmin_lo = cast<uint16_t>(x_blocks_(kDminOffset + 0, i));
            Expr dmin_hi = cast<uint16_t>(x_blocks_(kDminOffset + 1, i));
            Expr dmin = cast<float>(reinterpret<float16_t>(dmin_lo | (dmin_hi << 8)));

            Expr d1 = d * cast<float>(sc);
            Expr m1 = dmin * cast<float>(m);

            return d1 * cast<float>(nibble) - m1;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q4_KVecDotGenerator, q4_k_vec_dot)
