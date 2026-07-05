// From-scratch Halide reimplementation of GGML's Q8_0 quantize/dequantize
// kernels (see src/ggml-quants.c: quantize_row_q8_0_ref / dequantize_row_q8_0
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 34-byte block_q8_0 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-33: 32 signed int8 values, one byte per value (no nibble packing)
//
// Unlike Q4_0/Q5_0, the scale only needs the magnitude (amax), not a
// correlated signed value, since the encoding is a plain round(v * id) with
// no directional "snap to -N" trick. GGML uses round-half-away-from-zero
// (roundf) here, not the truncate-based "+0.5f then cast" trick used by the
// nibble-packed types -- Halide's round() matches C's std::round exactly
// (see Halide's IROperator.h doc comment on round()).
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK8_0 = 32;
constexpr int kBlockBytes = 2 + kQK8_0;  // 34

class Q8_0DequantizeGenerator : public Generator<Q8_0DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK8_0;
        Expr j = x % kQK8_0;

        // Bit-reinterpret the stored byte as signed (block_q8_0::qs is int8_t).
        Expr q = reinterpret<int8_t>(blocks_(2 + j, i));

        Expr d_lo = cast<uint16_t>(blocks_(0, i));
        Expr d_hi = cast<uint16_t>(blocks_(1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = cast<float>(q) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q8_0QuantizeGenerator : public Generator<Q8_0QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK8_0, "r");

        Func amax_f("amax_f");
        amax_f(i) = 0.0f;
        Expr v = x_(i * kQK8_0 + r);
        amax_f(i) = max(amax_f(i), abs(v));
        amax_f.compute_root();

        Func delta("delta");
        Expr d = amax_f(i) / 127.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        delta(i) = Tuple(d, id);
        delta.compute_root();

        Func qval("qval");  // 32 payload bytes per block, j in [0, 32)
        Expr id_ = delta(i)[1];
        Expr q = cast<int8_t>(round(x_(i * kQK8_0 + j) * id_));
        qval(j, i) = q;

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(delta(i)[0]));
        Expr d_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr d_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        // qval(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires qval at an out-of-range index for the header bytes.
        blocks_(byte, i) = select(
            byte == 0, d_byte0,
            byte == 1, d_byte1,
            reinterpret<uint8_t>(qval(clamp(byte - 2, 0, kQK8_0 - 1), i)));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

// vec_dot(Q8_0, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
// Both sides share the same block_q8_0 layout, so the same helper (see
// activation_dequant.h) is used for x and y.
class Q8_0VecDotGenerator : public Generator<Q8_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK8_0, "r");

        result_() = sum(ggml_halide::q8_0_value(x_blocks_, r) * ggml_halide::q8_0_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, kBlockBytes);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q8_0DequantizeGenerator, q8_0_dequantize)
HALIDE_REGISTER_GENERATOR(Q8_0QuantizeGenerator, q8_0_quantize)
HALIDE_REGISTER_GENERATOR(Q8_0VecDotGenerator, q8_0_vec_dot)
