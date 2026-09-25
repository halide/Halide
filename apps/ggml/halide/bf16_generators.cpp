// From-scratch Halide reimplementation of GGML's BF16 quantize/dequantize
// "kernels" (see src/ggml-impl.h: ggml_compute_fp32_to_bf16 /
// ggml_compute_bf16_to_fp32 upstream, as of GGML v0.15.3). Like F16, BF16
// isn't really a quantized format -- it's a 1-element/block cast, no
// header/payload split -- so this is just Halide's native bfloat16_t cast
// in both directions.
//
// GGML's bf16 encode is round-to-nearest-even truncation of the top 16 bits
// of the IEEE binary32 representation (with NaNs forced quiet); decode is
// a plain `bits << 16` reinterpretation. Halide's cast<bfloat16_t>/cast<float>
// compile to the same IEEE-mandated round-to-nearest-even truncation and
// zero-extension, so this matches bit-for-bit for all finite inputs (the
// only divergence possible is NaN payload/quieting, which the synthetic
// benchmark/test data never produces).
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

using namespace Halide;

namespace {

class BF16DequantizeGenerator : public Generator<BF16DequantizeGenerator> {
public:
    // Raw bf16 bit patterns, one uint16 per element (block size 1).
    Input<Buffer<uint16_t, 1>> x_{"x"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var i("i");
        y_(i) = cast<float>(reinterpret<bfloat16_t>(x_(i)));

        x_.dim(0).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class BF16QuantizeGenerator : public Generator<BF16QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // Raw bf16 bit patterns, one uint16 per element (block size 1).
    Output<Buffer<uint16_t, 1>> y_{"y"};

    void generate() {
        Var i("i");
        y_(i) = reinterpret<uint16_t>(cast<bfloat16_t>(x_(i)));

        x_.dim(0).set_min(0);
        y_.dim(0).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(BF16DequantizeGenerator, bf16_dequantize)
HALIDE_REGISTER_GENERATOR(BF16QuantizeGenerator, bf16_quantize)
