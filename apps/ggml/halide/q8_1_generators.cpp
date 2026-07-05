// From-scratch Halide reimplementation of GGML's Q8_1 quantize kernel (see
// src/ggml-quants.c: quantize_row_q8_1_ref upstream, as of GGML v0.15.3). No
// GGML headers are used here -- this file encodes its own understanding of
// the 36-byte block_q8_1 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-3:  fp16 sum 's' = d * sum(qs[i])
//   byte 4-35: 32 signed int8 values, one byte per value (no nibble packing)
//
// Q8_1 is an activation-only format: GGML has no public to_float for it (see
// include/ggml.h's type_traits table -- only from_float_ref is set), so
// there's nothing to dequantize against and no dequantize generator here,
// matching how the benchmark harness already treats such types (see
// providers/README.md's Q8_K discussion).
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK8_1 = 32;
constexpr int kBlockBytes = 4 + kQK8_1;  // 36

class Q8_1QuantizeGenerator : public Generator<Q8_1QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK8_1, "r");

        Func amax_f("amax_f");
        amax_f(i) = 0.0f;
        Expr v = x_(i * kQK8_1 + r);
        amax_f(i) = max(amax_f(i), abs(v));
        amax_f.compute_root();

        Func delta("delta");
        Expr d = amax_f(i) / 127.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        delta(i) = Tuple(d, id);
        delta.compute_root();

        Func qval("qval");  // 32 payload bytes per block, j in [0, 32)
        Expr id_ = delta(i)[1];
        qval(j, i) = cast<int8_t>(round(x_(i * kQK8_1 + j) * id_));
        qval.compute_root();  // referenced again below by the sum reduction

        Func sum_f("sum_f");
        sum_f(i) = 0;
        RDom rj(0, kQK8_1, "rj");
        sum_f(i) += cast<int32_t>(qval(rj, i));
        sum_f.compute_root();

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(delta(i)[0]));
        Expr d_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr d_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        Expr s = cast<float>(sum_f(i)) * delta(i)[0];
        Expr s_bits = reinterpret<uint16_t>(cast<float16_t>(s));
        Expr s_byte0 = cast<uint8_t>(s_bits & 0xff);
        Expr s_byte1 = cast<uint8_t>((s_bits >> 8) & 0xff);

        // qval(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires qval at an out-of-range index for the header bytes.
        blocks_(byte, i) = select(
            byte == 0, d_byte0,
            byte == 1, d_byte1,
            byte == 2, s_byte0,
            byte == 3, s_byte1,
            reinterpret<uint8_t>(qval(clamp(byte - 4, 0, kQK8_1 - 1), i)));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q8_1QuantizeGenerator, q8_1_quantize)
