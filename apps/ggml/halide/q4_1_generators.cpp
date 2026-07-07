// From-scratch Halide reimplementation of GGML's Q4_1 vec_dot kernel (see
// src/ggml-quants.c: quantize_row_q4_1_ref / dequantize_row_q4_1 upstream,
// as of GGML v0.15.3). No GGML headers are used here -- this file encodes
// its own understanding of the 20-byte block_q4_1 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-3:  fp16 min 'm'
//   byte 4-19: 16 bytes of packed 4-bit values (2 values per byte)
//
// Unlike Q4_0 (symmetric, centered nibbles), Q4_1 is affine: nibbles are
// unsigned [0,15] and dequantize is nibble*d + m, with no centering.
//
// Q4_1's quantize/dequantize kernels are no longer defined here -- they are
// the "q4_1_quantize"/"q4_1_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based generator in
// symmetric_quant_generators.cpp (see quant_components.h). Only vec_dot,
// which still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK4_1 = 32;
constexpr int kBlockBytes = 4 + kQK4_1 / 2;  // 20

// vec_dot(Q4_1, Q8_1): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q4_1VecDotGenerator : public Generator<Q4_1VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_1 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK4_1, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK4_1;
            Expr j = x % kQK4_1;
            Expr byte_idx = j % (kQK4_1 / 2);
            Expr is_low = j < (kQK4_1 / 2);
            Expr byte = x_blocks_(4 + byte_idx, i);
            Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);
            Expr d_lo = cast<uint16_t>(x_blocks_(0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr m_lo = cast<uint16_t>(x_blocks_(2, i));
            Expr m_hi = cast<uint16_t>(x_blocks_(3, i));
            Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));
            return cast<float>(nibble) * d + m;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_1_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 36);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q4_1VecDotGenerator, q4_1_vec_dot)
