// From-scratch Halide reimplementation of GGML's F16 quantize/dequantize
// "kernels" (see src/ggml.c: ggml_fp32_to_fp16_row / ggml_fp16_to_fp32_row
// upstream, as of GGML v0.15.3). F16 isn't really a quantized format -- it's
// a 1-element/block plain IEEE-754 binary16 cast, no header/payload split at
// all -- so unlike every other type here, this is just Halide's native
// float16_t cast in both directions.
//
// GGML's own conversion (GGML_COMPUTE_FP32_TO_FP16 / _FP16_TO_FP32 in
// src/ggml-impl.h) is a correctly-rounded (round-to-nearest-even) software
// IEEE binary16 <-> binary32 conversion; Halide's cast<float16_t>/cast<float>
// compiles to the same IEEE-mandated conversion, so this matches bit-for-bit.
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

using namespace Halide;

namespace {

class F16DequantizeGenerator : public Generator<F16DequantizeGenerator> {
public:
    // Raw fp16 bit patterns, one uint16 per element (block size 1).
    Input<Buffer<uint16_t, 1>> x_{"x"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var i("i");
        y_(i) = cast<float>(reinterpret<float16_t>(x_(i)));

        x_.dim(0).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class F16QuantizeGenerator : public Generator<F16QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // Raw fp16 bit patterns, one uint16 per element (block size 1).
    Output<Buffer<uint16_t, 1>> y_{"y"};

    void generate() {
        Var i("i");
        y_(i) = reinterpret<uint16_t>(cast<float16_t>(x_(i)));

        x_.dim(0).set_min(0);
        y_.dim(0).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(F16DequantizeGenerator, f16_dequantize)
HALIDE_REGISTER_GENERATOR(F16QuantizeGenerator, f16_quantize)
