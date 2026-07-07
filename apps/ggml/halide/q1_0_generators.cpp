// GGML's Q1_0 vec_dot kernel (see src/ggml-quants.c: quantize_row_q1_0_ref /
// dequantize_row_q1_0 upstream, as of GGML v0.15.3). No GGML headers are
// used here -- this file encodes its own understanding of the 18-byte
// block_q1_0 layout (a 128-element block, 1 bit per value):
//
//   byte 0-1:  fp16 delta 'd' (= mean(|x|) over the block, not a max-based
//              scale like every other type here)
//   byte 2-17: 16 bytes of sign bits, 1 bit per value (bit set -> +d,
//              clear -> -d) -- no magnitude information at all
//
// Q1_0's quantize/dequantize kernels are no longer defined here -- they are
// the "q1_0_quantize"/"q1_0_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based symmetric_quantize/
// symmetric_dequantize generators in symmetric_quant_generators.cpp (see
// quant_components.h's BitPack/RoundingMode::SignOnly/ScaleAnchor::MeanAbs,
// which reproduce Q1_0's sign-only, mean-abs-scaled quantizer faithfully --
// it's checked bit-exact; unlike the K-quants, this quantizer is closed-form,
// no extern stage needed). Only vec_dot, which still hand-rolls its own
// dequantize math, is unscheduled beyond the minimum Halide requires for
// legality -- scheduling for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 128;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kBlockBytes = kQsOffset + kQK / 8;  // 18

// vec_dot(Q1_0, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q1_0VecDotGenerator : public Generator<Q1_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_0 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK;
            Expr j = x % kQK;
            Expr byte_idx = j / 8;
            Expr bit_off = j % 8;
            Expr byte_val = x_blocks_(kQsOffset + byte_idx, i);
            Expr bit = (byte_val >> bit_off) & 1;
            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            return select(bit != 0, d, -d);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_0_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 34);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q1_0VecDotGenerator, q1_0_vec_dot)
