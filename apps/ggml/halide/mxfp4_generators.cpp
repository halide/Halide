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

using namespace Halide;

namespace {

constexpr int kQK = 32;
constexpr int kEOffset = 0;
constexpr int kQsOffset = 1;
constexpr int kBlockBytes = kQsOffset + kQK / 2;  // 17

// kvalues_mxfp4 = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12}
Expr lookup_mxfp4(Expr idx) {
    return select(idx == 0, 0, idx == 1, 1, idx == 2, 2, idx == 3, 3,
                  idx == 4, 4, idx == 5, 6, idx == 6, 8, idx == 7, 12,
                  idx == 8, 0, idx == 9, -1, idx == 10, -2, idx == 11, -3,
                  idx == 12, -4, idx == 13, -6, idx == 14, -8, -12);
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

}  // namespace

HALIDE_REGISTER_GENERATOR(MXFP4DequantizeGenerator, mxfp4_dequantize)
HALIDE_REGISTER_GENERATOR(MXFP4QuantizeGenerator, mxfp4_quantize)
