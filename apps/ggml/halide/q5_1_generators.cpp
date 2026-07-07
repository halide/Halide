// From-scratch Halide reimplementation of GGML's Q5_1 vec_dot kernel (see
// src/ggml-quants.c: quantize_row_q5_1_ref / dequantize_row_q5_1 upstream,
// as of GGML v0.15.3). No GGML headers are used here -- this file encodes
// its own understanding of the 24-byte block_q5_1 layout:
//
//   byte 0-1:   fp16 delta 'd'
//   byte 2-3:   fp16 min 'm'
//   byte 4-7:   qh, a little-endian uint32 holding the 5th (high) bit of all
//               32 quantized values, packed one bit per value
//   byte 8-23:  16 bytes of packed 4-bit low bits (2 values per byte)
//
// Affine like Q4_1 (unsigned value, dequantize is value*d + m, no
// centering), but with an extra high bit per value stored out-of-band in qh,
// like Q5_0. Note: unlike every other legacy type here, GGML's reference
// quantizer does NOT clamp the packed value to its 5-bit range.
//
// Q5_1's quantize/dequantize kernels are no longer defined here -- they are
// the "q5_1_quantize"/"q5_1_dequantize" GENERATOR_ARGS instantiations of the
// generic, reusable Approximation-based generator in
// symmetric_quant_generators.cpp (see quant_components.h's AffineRounding::
// UnclampedUint8, which reproduces the no-clamp behavior faithfully -- it's
// checked bit-exact). Only vec_dot, which still hand-rolls its own
// dequantize math, is unscheduled beyond the minimum Halide requires for
// legality -- scheduling for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK5_1 = 32;
constexpr int kBlockBytes = 4 + 4 + kQK5_1 / 2;  // 24

// vec_dot(Q5_1, Q8_1): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class Q5_1VecDotGenerator : public Generator<Q5_1VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_1 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK5_1, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK5_1;
            Expr j = x % kQK5_1;
            Expr byte_idx = j % (kQK5_1 / 2);
            Expr is_low = j < (kQK5_1 / 2);
            Expr byte = x_blocks_(8 + byte_idx, i);
            Expr nibble = cast<uint32_t>(select(is_low, byte & 0x0f, (byte >> 4) & 0x0f));
            Expr qh = cast<uint32_t>(x_blocks_(4, i)) |
                      (cast<uint32_t>(x_blocks_(5, i)) << 8) |
                      (cast<uint32_t>(x_blocks_(6, i)) << 16) |
                      (cast<uint32_t>(x_blocks_(7, i)) << 24);
            Expr xh_lo = ((qh >> (byte_idx + 0)) << 4) & 0x10u;
            Expr xh_hi = (qh >> (byte_idx + 12)) & 0x10u;
            Expr xh = select(is_low, xh_lo, xh_hi);
            Expr val = cast<float>(nibble | xh);
            Expr d_lo = cast<uint16_t>(x_blocks_(0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            Expr m_lo = cast<uint16_t>(x_blocks_(2, i));
            Expr m_hi = cast<uint16_t>(x_blocks_(3, i));
            Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));
            return val * d + m;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_1_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 36);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q5_1VecDotGenerator, q5_1_vec_dot)
