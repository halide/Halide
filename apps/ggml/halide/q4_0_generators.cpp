// From-scratch Halide reimplementation of GGML's Q4_0 quantize/dequantize
// kernels (see src/ggml-quants.c: quantize_row_q4_0_ref / dequantize_row_q4_0
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 18-byte block_q4_0 layout:
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-17: 16 bytes of packed 4-bit values (2 values per byte)
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK4_0 = 32;
constexpr int kBlockBytes = 2 + kQK4_0 / 2;  // 18

class Q4_0DequantizeGenerator : public Generator<Q4_0DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK4_0;
        Expr j = x % kQK4_0;
        Expr byte_idx = j % (kQK4_0 / 2);
        Expr is_low = j < (kQK4_0 / 2);

        Expr byte = blocks_(2 + byte_idx, i);
        Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);
        Expr q = cast<int32_t>(nibble) - 8;

        Expr lo = cast<uint16_t>(blocks_(0, i));
        Expr hi = cast<uint16_t>(blocks_(1, i));
        Expr d = cast<float>(reinterpret<float16_t>(lo | (hi << 8)));

        y_(x) = cast<float>(q) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class Q4_0QuantizeGenerator : public Generator<Q4_0QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK4_0, "r");

        // Per-block reduction: track the signed value with the largest
        // magnitude seen so far, mirroring GGML's single left-to-right loop
        // (a strict '<' comparison, so ties keep the first-seen value).
        Func stat("stat");
        stat(i) = Tuple(0.0f, 0.0f);  // {amax, max}
        Expr v = x_(i * kQK4_0 + r);
        Expr take = abs(v) > stat(i)[0];
        stat(i) = Tuple(select(take, abs(v), stat(i)[0]),
                        select(take, v, stat(i)[1]));
        stat.compute_root();

        Func delta("delta");
        Expr max_val = stat(i)[1];
        Expr d = max_val * (-1.0f / 8.0f);
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        delta(i) = Tuple(d, id);
        delta.compute_root();

        Func packed("packed");  // 16 payload bytes per block, j in [0, 16)
        Expr id_ = delta(i)[1];
        Expr x0 = x_(i * kQK4_0 + j) * id_;
        Expr x1 = x_(i * kQK4_0 + kQK4_0 / 2 + j) * id_;
        Expr xi0 = min(cast<int32_t>(cast<int8_t>(x0 + 8.5f)), 15);
        Expr xi1 = min(cast<int32_t>(cast<int8_t>(x1 + 8.5f)), 15);
        packed(j, i) = cast<uint8_t>(xi0) | cast<uint8_t>(xi1 << 4);

        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(delta(i)[0]));
        Expr delta_byte0 = cast<uint8_t>(d_bits & 0xff);
        Expr delta_byte1 = cast<uint8_t>((d_bits >> 8) & 0xff);

        // packed(...) is an argument to select() and so is evaluated
        // unconditionally for every byte (Halide's select() isn't
        // short-circuiting) -- clamp its index so bounds inference never
        // requires packed at the out-of-range byte-2 in {-2, -1} that occur
        // when byte is 0 or 1. Those clamped evaluations are computed but
        // discarded by the select. Same idiom as test/correctness/argmax.cpp.
        blocks_(byte, i) = select(
            byte == 0, delta_byte0,
            byte == 1, delta_byte1,
            packed(clamp(byte - 2, 0, kQK4_0 / 2 - 1), i));

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

// vec_dot(Q4_0, Q8_0): a plain elementwise dequantize-then-multiply-and-sum
// dot product -- mathematically equivalent to GGML's integer-factored
// accumulation (d_x*d_y*sum(qx*qy)), just re-grouped; vec_dot is a
// tolerance-checked benchmark, not bit-exact, so this is a valid
// reimplementation of the same reduction. See activation_dequant.h for why
// the Q8_0 (activation) side is a shared helper instead of being duplicated
// per weight type.
class Q4_0VecDotGenerator : public Generator<Q4_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_0 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK4_0, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK4_0;
            Expr j = x % kQK4_0;
            Expr byte_idx = j % (kQK4_0 / 2);
            Expr is_low = j < (kQK4_0 / 2);
            Expr byte = x_blocks_(2 + byte_idx, i);
            Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);
            Expr q = cast<int32_t>(nibble) - 8;
            Expr lo = cast<uint16_t>(x_blocks_(0, i));
            Expr hi = cast<uint16_t>(x_blocks_(1, i));
            Expr d = cast<float>(reinterpret<float16_t>(lo | (hi << 8)));
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

HALIDE_REGISTER_GENERATOR(Q4_0DequantizeGenerator, q4_0_dequantize)
HALIDE_REGISTER_GENERATOR(Q4_0QuantizeGenerator, q4_0_quantize)
HALIDE_REGISTER_GENERATOR(Q4_0VecDotGenerator, q4_0_vec_dot)
