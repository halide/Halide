// From-scratch Halide reimplementation of GGML's MXFP4 dequantize kernel
// (see src/ggml-quants.c: dequantize_row_mxfp4 upstream, as of GGML
// v0.15.3). No GGML headers are used by the dequantize generator -- it
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
// Quantize is NOT reimplemented here: GGML's reference quantizer derives
// the exponent via floor(log2(amax)), a transcendental computation not
// guaranteed to be bit-reproducible against a from-scratch implementation
// -- see ggml_extern_quantize.cpp for why and how this Func instead calls
// out to GGML's own reference via a Halide extern stage.
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

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

class MXFP4DequantizeGenerator : public Generator<MXFP4DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK;
        Expr j = x % kQK;

        Expr byte_idx = j % (kQK / 2);
        Expr is_low = j < (kQK / 2);
        Expr byte = blocks_(kQsOffset + byte_idx, i);
        Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));
        Expr val = lookup_mxfp4(nibble);

        // ggml_e8m0_to_fp32_half(e): pure bit construction, no rounding.
        Expr e = blocks_(kEOffset, i);
        Expr bits = select(cast<uint32_t>(e) < 2,
                           cast<uint32_t>(0x00200000) << cast<uint32_t>(e),
                           (cast<uint32_t>(e) - 1) << 23);
        Expr d = reinterpret<float>(bits);

        y_(x) = cast<float>(val) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class MXFP4QuantizeGenerator : public Generator<MXFP4QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("mxfp4_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

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

HALIDE_REGISTER_GENERATOR(MXFP4DequantizeGenerator, mxfp4_dequantize)
HALIDE_REGISTER_GENERATOR(MXFP4QuantizeGenerator, mxfp4_quantize)
HALIDE_REGISTER_GENERATOR(MXFP4VecDotGenerator, mxfp4_vec_dot)
