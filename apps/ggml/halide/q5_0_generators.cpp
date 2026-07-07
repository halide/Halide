// From-scratch Halide reimplementation of GGML's Q5_0 vec_dot kernel (see
// src/ggml-quants.c: quantize_row_q5_0_ref / dequantize_row_q5_0 upstream,
// as of GGML v0.15.3). No GGML headers are used here -- this file encodes
// its own understanding of the 22-byte block_q5_0 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-5:  qh, a little-endian uint32 holding the 5th (high) bit of all
//              32 quantized values, packed one bit per value
//   byte 6-21: 16 bytes of packed 4-bit low bits (2 values per byte)
//
// Symmetric like Q4_0 (5-bit signed value centered around 16, scale
// max/-16), but with an extra high bit per value stored out-of-band in qh.
//
// Q5_0's quantize/dequantize kernels are no longer defined here -- they are
// the "q5_0_quantize"/"q5_0_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based generator in
// symmetric_quant_generators.cpp (see quant_components.h). Only vec_dot,
// which still hand-rolls its own dequantize math, is unscheduled beyond the
// minimum Halide requires for legality -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK5_0 = 32;
constexpr int kBlockBytes = 2 + 4 + kQK5_0 / 2;  // 22

// vec_dot(Q5_0, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q5_0VecDotGenerator : public Generator<Q5_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_0 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK5_0, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK5_0;
            Expr j = x % kQK5_0;
            Expr byte_idx = j % (kQK5_0 / 2);
            Expr is_low = j < (kQK5_0 / 2);
            Expr byte = x_blocks_(6 + byte_idx, i);
            Expr nibble = cast<uint32_t>(select(is_low, byte & 0x0f, (byte >> 4) & 0x0f));
            Expr qh = cast<uint32_t>(x_blocks_(2, i)) |
                      (cast<uint32_t>(x_blocks_(3, i)) << 8) |
                      (cast<uint32_t>(x_blocks_(4, i)) << 16) |
                      (cast<uint32_t>(x_blocks_(5, i)) << 24);
            Expr xh_lo = ((qh >> (byte_idx + 0)) << 4) & 0x10u;
            Expr xh_hi = (qh >> (byte_idx + 12)) & 0x10u;
            Expr xh = select(is_low, xh_lo, xh_hi);
            Expr q = cast<int32_t>(nibble | xh) - 16;
            Expr d_lo = cast<uint16_t>(x_blocks_(0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            return cast<float>(q) * d;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_0_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 34);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q5_0VecDotGenerator, q5_0_vec_dot)
