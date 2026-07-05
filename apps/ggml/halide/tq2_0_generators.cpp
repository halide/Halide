// From-scratch Halide reimplementation of GGML's TQ2_0 dequantize kernel
// (see src/ggml-quants.c: dequantize_row_tq2_0 upstream, as of GGML
// v0.15.3). No GGML headers are used by the dequantize generator -- it
// encodes its own understanding of the 66-byte block_tq2_0 layout (a
// 256-element ternary superblock, qs BEFORE d unlike every other type
// here):
//
//   byte 0-63: qs[64] -- 2 bits per element (4 elements per byte), each in
//              {0,1,2} representing {-1,0,1}
//   byte 64-65: fp16 delta 'd'
//
// Much simpler than TQ1_0: a plain 2-bit field per value, no base-3 byte
// packing trick.
//
// Quantize is NOT reimplemented here: see ggml_extern_quantize.cpp for how
// this Func instead calls out to GGML's own reference via a Halide extern
// stage (kept consistent with TQ1_0's treatment, even though TQ2_0's
// quantizer is simpler).
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kQsOffset = 0;
constexpr int kDOffset = kQK_K / 4;        // 64
constexpr int kBlockBytes = kDOffset + 2;  // 66

class TQ2_0DequantizeGenerator : public Generator<TQ2_0DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;

        Expr half = gi / 128;  // which 32-byte qs window, 0 or 1
        Expr local = gi % 128;
        Expr l = local / 32;  // which 2-bit plane, 0..3
        Expr m = local % 32;  // byte offset within the window

        Expr byte_idx = kQsOffset + half * 32 + m;
        Expr byte_val = blocks_(byte_idx, i);
        Expr q = cast<int32_t>((byte_val >> (l * 2)) & 3);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = cast<float>(q - 1) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class TQ2_0QuantizeGenerator : public Generator<TQ2_0QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("tq2_0_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TQ2_0DequantizeGenerator, tq2_0_dequantize)
HALIDE_REGISTER_GENERATOR(TQ2_0QuantizeGenerator, tq2_0_quantize)
