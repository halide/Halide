// From-scratch Halide reimplementation of GGML's Q4_1 quantize/dequantize
// kernels (see src/ggml-quants.c: quantize_row_q4_1_ref / dequantize_row_q4_1
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 20-byte block_q4_1 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-3:  fp16 min 'm'
//   byte 4-19: 16 bytes of packed 4-bit values (2 values per byte)
//
// Unlike Q4_0 (symmetric, centered nibbles), Q4_1 is affine: nibbles are
// unsigned [0,15] and dequantize is nibble*d + m, with no centering.
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK4_1 = 32;
constexpr int kBlockBytes = 4 + kQK4_1 / 2;  // 20

class Q4_1DequantizeGenerator : public Generator<Q4_1DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK4_1;
        Expr j = x % kQK4_1;
        Expr byte_idx = j % (kQK4_1 / 2);
        Expr is_low = j < (kQK4_1 / 2);

        Expr byte = blocks_(4 + byte_idx, i);
        Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);

        Expr d_lo = cast<uint16_t>(blocks_(0, i));
        Expr d_hi = cast<uint16_t>(blocks_(1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        Expr m_lo = cast<uint16_t>(blocks_(2, i));
        Expr m_hi = cast<uint16_t>(blocks_(3, i));
        Expr m = cast<float>(reinterpret<float16_t>(m_lo | (m_hi << 8)));

        y_(x) = cast<float>(nibble) * d + m;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q4_1QuantizeGenerator : public Generator<Q4_1QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK4_1, "r");

        // Plain min/max reduction (unlike Q4_0/Q5_0, Q4_1 doesn't need the
        // signed value that achieved the extreme -- min and max are used
        // independently to form an affine range).
        Func stat("stat");
        stat(i) = Tuple(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());  // {min, max}
        Expr v = x_(i * kQK4_1 + r);
        stat(i) = Tuple(min(stat(i)[0], v), max(stat(i)[1], v));
        stat.compute_root();

        Func delta("delta");
        Expr range = stat(i)[1] - stat(i)[0];
        Expr d = range / 15.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        delta(i) = Tuple(d, id);
        delta.compute_root();

        Func packed("packed");  // 16 payload bytes per block, j in [0, 16)
        Expr minv = stat(i)[0];
        Expr id_ = delta(i)[1];
        Expr x0 = (x_(i * kQK4_1 + j) - minv) * id_;
        Expr x1 = (x_(i * kQK4_1 + kQK4_1 / 2 + j) - minv) * id_;
        Expr xi0 = min(cast<int32_t>(cast<int8_t>(x0 + 0.5f)), 15);
        Expr xi1 = min(cast<int32_t>(cast<int8_t>(x1 + 0.5f)), 15);
        packed(j, i) = cast<uint8_t>(xi0) | cast<uint8_t>(xi1 << 4);

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(delta(i)[0]));
        Expr d_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr d_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        Expr m_bits = reinterpret<uint16_t>(cast<float16_t>(stat(i)[0]));
        Expr m_byte0 = cast<uint8_t>(m_bits & 0xff);
        Expr m_byte1 = cast<uint8_t>((m_bits >> 8) & 0xff);

        // packed(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires packed at an out-of-range index for the header bytes.
        blocks_(byte, i) = select(
            byte == 0, d_byte0,
            byte == 1, d_byte1,
            byte == 2, m_byte0,
            byte == 3, m_byte1,
            packed(clamp(byte - 4, 0, kQK4_1 / 2 - 1), i));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

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

HALIDE_REGISTER_GENERATOR(Q4_1DequantizeGenerator, q4_1_dequantize)
HALIDE_REGISTER_GENERATOR(Q4_1QuantizeGenerator, q4_1_quantize)
HALIDE_REGISTER_GENERATOR(Q4_1VecDotGenerator, q4_1_vec_dot)
