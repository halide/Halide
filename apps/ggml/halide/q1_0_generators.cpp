// From-scratch Halide reimplementation of GGML's Q1_0 quantize/dequantize
// kernels (see src/ggml-quants.c: quantize_row_q1_0_ref /
// dequantize_row_q1_0 upstream, as of GGML v0.15.3). No GGML headers are
// used here -- this file encodes its own understanding of the 18-byte
// block_q1_0 layout (a 128-element block, 1 bit per value):
//
//   byte 0-1:  fp16 delta 'd' (= mean(|x|) over the block, not a max-based
//              scale like every other type here)
//   byte 2-17: 16 bytes of sign bits, 1 bit per value (bit set -> +d,
//              clear -> -d) -- no magnitude information at all
//
// Unlike the K-quants, Q1_0's quantizer is closed-form (no iterative search)
// so both directions are implemented natively in Halide -- no extern stage
// needed here.
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 128;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kBlockBytes = kQsOffset + kQK / 8;  // 18

class Q1_0DequantizeGenerator : public Generator<Q1_0DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK;
        Expr j = x % kQK;

        Expr byte_idx = j / 8;
        Expr bit_off = j % 8;
        Expr byte_val = blocks_(kQsOffset + byte_idx, i);
        Expr bit = (byte_val >> bit_off) & 1;

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = select(bit != 0, d, -d);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q1_0QuantizeGenerator : public Generator<Q1_0QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), byteidx("byteidx"), byte("byte");
        RDom r(0, kQK, "r");

        Func sum_abs("sum_abs");
        sum_abs(i) = 0.0f;
        sum_abs(i) += abs(x_(i * kQK + r));
        sum_abs.compute_root();

        Expr d = sum_abs(i) / cast<float>(kQK);

        Func packed("packed");  // 16 payload bytes per block, byteidx in [0, 16)
        packed(byteidx, i) = cast<uint8_t>(0);
        RDom rb(0, 8, "rb");
        Expr elem = x_(i * kQK + byteidx * 8 + rb);
        Expr bit_val = cast<uint8_t>(select(elem >= 0.0f, 1 << rb, 0));
        packed(byteidx, i) = packed(byteidx, i) | bit_val;
        packed.compute_root();

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(d));
        Expr d_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr d_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        // packed(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires packed at an out-of-range index for the header bytes.
        blocks_(byte, i) = select(
            byte == 0, d_byte0,
            byte == 1, d_byte1,
            packed(clamp(byte - 2, 0, kQK / 8 - 1), i));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

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

HALIDE_REGISTER_GENERATOR(Q1_0DequantizeGenerator, q1_0_dequantize)
HALIDE_REGISTER_GENERATOR(Q1_0QuantizeGenerator, q1_0_quantize)
HALIDE_REGISTER_GENERATOR(Q1_0VecDotGenerator, q1_0_vec_dot)
