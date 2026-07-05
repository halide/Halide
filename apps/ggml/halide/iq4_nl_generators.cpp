// From-scratch Halide reimplementation of GGML's IQ4_NL dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq4_nl upstream, as of GGML
// v0.15.3). No GGML headers are used by the dequantize generator -- it
// encodes its own understanding of the 18-byte block_iq4_nl layout (a
// 32-element block):
//
//   byte 0-1:  fp16 delta 'd'
//   byte 2-17: qs[16] -- 4 bits per element (2 elements per byte), each a
//              codebook index into the fixed 16-value kvalues_iq4nl table
//              (a non-uniform, non-linear codebook -- hence "NL")
//
// Quantize is NOT reimplemented here: GGML's reference quantizer runs a
// per-block error-minimizing search over that codebook combined with a
// scale refinement loop (very similar in spirit to the K-quants' iterative
// search), which is deferred -- see ggml_extern_quantize.cpp for why and
// how this Func instead calls out to GGML's own reference via a Halide
// extern stage.
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 32;
constexpr int kDOffset = 0;
constexpr int kQsOffset = 2;
constexpr int kBlockBytes = kQsOffset + kQK / 2;  // 18

// kvalues_iq4nl = {-127, -104, -83, -65, -49, -35, -22, -10,
//                     1,   13,  25,  38,  53,  69,  89, 113}
Expr lookup_iq4nl(Expr idx) {
    return select(idx == 0, -127, idx == 1, -104, idx == 2, -83, idx == 3, -65,
                  idx == 4, -49, idx == 5, -35, idx == 6, -22, idx == 7, -10,
                  idx == 8, 1, idx == 9, 13, idx == 10, 25, idx == 11, 38,
                  idx == 12, 53, idx == 13, 69, idx == 14, 89, 113);
}

class IQ4_NLDequantizeGenerator : public Generator<IQ4_NLDequantizeGenerator> {
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
        Expr val = lookup_iq4nl(nibble);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = d * cast<float>(val);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class IQ4_NLQuantizeGenerator : public Generator<IQ4_NLQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("iq4_nl_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

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

HALIDE_REGISTER_GENERATOR(IQ4_NLDequantizeGenerator, iq4_nl_dequantize)
HALIDE_REGISTER_GENERATOR(IQ4_NLQuantizeGenerator, iq4_nl_quantize)
HALIDE_REGISTER_GENERATOR(IQ4_NLVecDotGenerator, iq4_nl_vec_dot)
