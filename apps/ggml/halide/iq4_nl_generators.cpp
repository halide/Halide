// GGML's IQ4_NL vec_dot kernel (see src/ggml-quants.c: dequantize_row_iq4_nl
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 18-byte block_iq4_nl layout (a
// 32-element block):
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-17: qs[16] -- 4 bits per element (2 elements per byte), each a
//              codebook index into the fixed 16-value kvalues_iq4nl table
//              (a non-uniform, non-linear codebook -- hence "NL")
//
// IQ4_NL's quantize/dequantize kernels are no longer defined here -- they
// are the "iq4_nl_quantize"/"iq4_nl_dequantize" GENERATOR_ARGS
// instantiations of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// LookupTableQuantize; quantize still delegates to GGML's own reference via
// a Halide extern stage there too -- see ggml_extern_quantize.cpp for why).
// Only vec_dot, which still hand-rolls its own dequantize math, is
// unscheduled beyond the minimum Halide requires for legality -- scheduling
// for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 32;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kBlockBytes = kQsOffset + kQK / 2;  // 18

// kvalues_iq4nl: a small fixed codebook, embedded as compile-time constant
// data (not an Input<Buffer<>>/ImageParam -- this Buffer is baked into the
// compiled pipeline as a read-only resource) rather than a deep select()
// chain.
Expr lookup_iq4nl(Expr idx) {
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> lut(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl");
    return cast<int32_t>(lut(idx));
}

// vec_dot(IQ4_NL, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ4_NLVecDotGenerator : public Generator<IQ4_NLVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_0 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK;
            Expr j = x % kQK;
            Expr byte_idx = j % (kQK / 2);
            Expr is_low = j < (kQK / 2);
            Expr byte = x_blocks_(kQsOffset + byte_idx, i);
            Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));
            Expr val = lookup_iq4nl(nibble);
            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            return d * cast<float>(val);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_0_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 34);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ4_NLVecDotGenerator, iq4_nl_vec_dot)
