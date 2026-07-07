// GGML's Q5_K vec_dot kernel (see src/ggml-quants.c: dequantize_row_q5_K /
// get_scale_min_k4 upstream, as of GGML v0.15.3). No GGML headers are used
// here -- this file encodes its own understanding of the 176-byte
// block_q5_K layout (a 256-element superblock, 8 sub-groups of 32 elements
// each):
//
//   byte 0-1:    fp16 delta 'd' (super-block scale for the scales)
//   byte 2-3:    fp16 'dmin' (super-block scale for the mins)
//   byte 4-15:   scales[12] -- same bit-interleaved 6-bit (scale, min) pairs
//                as Q4_K's get_scale_min_k4 (see q4_k_generators.cpp)
//   byte 16-47:  qh[32] -- the 5th (high) bit of every value, 1 bit each,
//                reused across all 4 iterations via a rotating bit position
//   byte 48-175: qs[128] -- low 4 bits of every value (2 elements per byte)
//
// Q5_K's quantize/dequantize kernels are no longer defined here -- they are
// the "q5_k_quantize"/"q5_k_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based k_quant_quantize/k_quant_dequantize
// generators in k_quant_generators.cpp (see quant_components.h's
// KQuantDequantize/K4ScaleMinPack/CombinedBitsCode/PlanarBitPack/
// PlanarBitPack, which reproduce this exact bit-interleaved scale/min
// packing and nibble+high-bit code layout; quantize still delegates to
// GGML's own reference via a Halide extern stage there too -- see
// ggml_extern_quantize.cpp for why). Only vec_dot, which still hand-rolls
// its own dequantize math, is unscheduled beyond the minimum Halide
// requires for legality -- scheduling for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kDOffset = 0;
constexpr int kDminOffset = 2;
constexpr int kScalesOffset = 4;
constexpr int kQhOffset = kScalesOffset + 12;       // 16
constexpr int kQsOffset = kQhOffset + kQK_K / 8;    // 48
constexpr int kBlockBytes = kQsOffset + kQK_K / 2;  // 176

// vec_dot(Q5_K, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q5_KVecDotGenerator : public Generator<Q5_KVecDotGenerator> {
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

            Expr qh_byte = x_blocks_(kQhOffset + l, i);
            Expr u_bit = select(half64 == 0,
                                cast<uint32_t>(1) << (2 * iter),
                                cast<uint32_t>(1) << (2 * iter + 1));
            Expr high_bit = select((cast<uint32_t>(qh_byte) & u_bit) != 0, 16, 0);

            Expr value = nibble + high_bit;

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

            return d1 * cast<float>(value) - m1;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q5_KVecDotGenerator, q5_k_vec_dot)
