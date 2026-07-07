// GGML's MXFP4 vec_dot kernel (see src/ggml-quants.c: dequantize_row_mxfp4
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
// encodes its own understanding of the 17-byte block_mxfp4 layout (a
// 32-element block, OCP Microscaling FP4 E2M1 with a shared E8M0 exponent):
//
//   byte 0:     'e' -- an 8-bit power-of-two exponent (E8M0), shared by the
//               whole block
//   byte 1-16:  qs[16] -- 4 bits per element (2 elements per byte), each a
//               codebook index into the fixed 16-value kvalues_mxfp4 table
//
// GGML derives the block's float scale from 'e' via ggml_e8m0_to_fp32_half,
// a pure bit-construction (no rounding) -- reproduced exactly here.
//
// MXFP4's quantize/dequantize kernels are no longer defined here -- they
// are the "mxfp4_quantize"/"mxfp4_dequantize" GENERATOR_ARGS instantiations
// of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// LookupTableQuantize/E8M0Pack; quantize still delegates to GGML's own
// reference via a Halide extern stage there too, since the exponent is
// derived via floor(log2(amax)), a transcendental computation not
// guaranteed to be bit-reproducible against a from-scratch implementation
// -- see ggml_extern_quantize.cpp for why). Only vec_dot, which still
// hand-rolls its own dequantize math, is unscheduled beyond the minimum
// Halide requires for legality -- scheduling for performance is a later
// step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 32;
constexpr int kEOffset = 0;
constexpr int kQsOffset = 1;
constexpr int kBlockBytes = kQsOffset + kQK / 2;  // 17

// kvalues_mxfp4: a small fixed codebook, embedded as compile-time constant
// data (not an Input<Buffer<>>/ImageParam -- this Buffer is baked into the
// compiled pipeline as a read-only resource) rather than a deep select()
// chain.
Expr lookup_mxfp4(Expr idx) {
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> lut(const_cast<int8_t *>(kValues), 16, "kvalues_mxfp4");
    return cast<int32_t>(lut(idx));
}

// vec_dot(MXFP4, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class MXFP4VecDotGenerator : public Generator<MXFP4VecDotGenerator> {
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
            Expr val = lookup_mxfp4(nibble);
            Expr e = x_blocks_(kEOffset, i);
            Expr bits = select(cast<uint32_t>(e) < 2,
                               cast<uint32_t>(0x00200000) << cast<uint32_t>(e),
                               (cast<uint32_t>(e) - 1) << 23);
            Expr d = reinterpret<float>(bits);
            return cast<float>(val) * d;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_0_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 34);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MXFP4VecDotGenerator, mxfp4_vec_dot)
