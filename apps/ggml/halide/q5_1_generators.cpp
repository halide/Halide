// From-scratch Halide reimplementation of GGML's Q5_1 quantize/dequantize
// kernels (see src/ggml-quants.c: quantize_row_q5_1_ref / dequantize_row_q5_1
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 24-byte block_q5_1 layout:
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
// quantizer does NOT clamp the packed value to its 5-bit range -- reproduced
// faithfully (no min()/clamp) since quantize output is checked bit-exact.
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK5_1 = 32;
constexpr int kBlockBytes = 4 + 4 + kQK5_1 / 2;  // 24

class Q5_1DequantizeGenerator : public Generator<Q5_1DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK5_1;
        Expr j = x % kQK5_1;
        Expr byte_idx = j % (kQK5_1 / 2);
        Expr is_low = j < (kQK5_1 / 2);

        Expr byte = blocks_(8 + byte_idx, i);
        Expr nibble = cast<uint32_t>(select(is_low, byte & 0x0f, (byte >> 4) & 0x0f));

        Expr qh = cast<uint32_t>(blocks_(4, i)) |
                  (cast<uint32_t>(blocks_(5, i)) << 8) |
                  (cast<uint32_t>(blocks_(6, i)) << 16) |
                  (cast<uint32_t>(blocks_(7, i)) << 24);
        Expr xh_lo = ((qh >> (byte_idx + 0)) << 4) & 0x10u;
        Expr xh_hi = (qh >> (byte_idx + 12)) & 0x10u;
        Expr xh = select(is_low, xh_lo, xh_hi);

        Expr val = cast<float>(nibble | xh);  // no -16 centering: Q5_1 is affine

        Expr d_lo = cast<uint16_t>(blocks_(0, i));
        Expr d_hi = cast<uint16_t>(blocks_(1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr m_lo = cast<uint16_t>(blocks_(2, i));
        Expr m_hi = cast<uint16_t>(blocks_(3, i));
        Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));

        y_(x) = val * d + m;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q5_1QuantizeGenerator : public Generator<Q5_1QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK5_1, "r");

        // Plain min/max reduction (like Q4_1).
        Func stat("stat");
        stat(i) = Tuple(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());  // {min, max}
        Expr v = x_(i * kQK5_1 + r);
        stat(i) = Tuple(min(stat(i)[0], v), max(stat(i)[1], v));
        stat.compute_root();

        Func delta("delta");
        Expr range = stat(i)[1] - stat(i)[0];
        Expr d = range / 31.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        delta(i) = Tuple(d, id);
        delta.compute_root();

        // {xi0, xi1}: the two 5-bit (0..31) quantized values, before
        // splitting into a 4-bit nibble and a 5th bit stored in qh. Note:
        // matching GGML exactly, no clamp here -- a direct truncating cast.
        Func xi("xi");
        Expr minv = stat(i)[0];
        Expr id_ = delta(i)[1];
        Expr x0 = (x_(i * kQK5_1 + j) - minv) * id_;
        Expr x1 = (x_(i * kQK5_1 + kQK5_1 / 2 + j) - minv) * id_;
        Expr xi0 = cast<int32_t>(cast<uint8_t>(x0 + 0.5f));
        Expr xi1 = cast<int32_t>(cast<uint8_t>(x1 + 0.5f));
        xi(j, i) = Tuple(xi0, xi1);
        xi.compute_root();

        Func packed("packed");  // 16 payload bytes per block, j in [0, 16)
        Expr p0 = cast<uint32_t>(xi(j, i)[0]);
        Expr p1 = cast<uint32_t>(xi(j, i)[1]);
        packed(j, i) = cast<uint8_t>(p0 & 0x0f) | cast<uint8_t>((p1 & 0x0f) << 4);

        // qh: the 5th (0x10) bit of every value, OR-accumulated into a
        // 32-bit word across all 16 (low, high) pairs in the block.
        Func qh_accum("qh_accum");
        qh_accum(i) = cast<uint32_t>(0);
        RDom rj(0, kQK5_1 / 2, "rj");
        Expr bit_lo = (cast<uint32_t>(xi(rj, i)[0]) & 0x10u) >> 4;
        Expr bit_hi = (cast<uint32_t>(xi(rj, i)[1]) & 0x10u) >> 4;
        qh_accum(i) = qh_accum(i) | (bit_lo << rj) | (bit_hi << (rj + kQK5_1 / 2));
        qh_accum.compute_root();

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(delta(i)[0]));
        Expr d_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr d_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        Expr m_bits = reinterpret<uint16_t>(cast<float16_t>(stat(i)[0]));
        Expr m_byte0 = cast<uint8_t>(m_bits & 0xff);
        Expr m_byte1 = cast<uint8_t>((m_bits >> 8) & 0xff);

        Expr qh_val = qh_accum(i);
        Expr qh_byte0 = cast<uint8_t>(qh_val & 0xff);
        Expr qh_byte1 = cast<uint8_t>((qh_val >> 8) & 0xff);
        Expr qh_byte2 = cast<uint8_t>((qh_val >> 16) & 0xff);
        Expr qh_byte3 = cast<uint8_t>((qh_val >> 24) & 0xff);

        // packed(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires packed at an out-of-range index for the header bytes.
        blocks_(byte, i) = select(
            byte == 0, d_byte0,
            byte == 1, d_byte1,
            byte == 2, m_byte0,
            byte == 3, m_byte1,
            byte == 4, qh_byte0,
            byte == 5, qh_byte1,
            byte == 6, qh_byte2,
            byte == 7, qh_byte3,
            packed(clamp(byte - 8, 0, kQK5_1 / 2 - 1), i));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

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

HALIDE_REGISTER_GENERATOR(Q5_1DequantizeGenerator, q5_1_dequantize)
HALIDE_REGISTER_GENERATOR(Q5_1QuantizeGenerator, q5_1_quantize)
HALIDE_REGISTER_GENERATOR(Q5_1VecDotGenerator, q5_1_vec_dot)
