// GGML's TQ2_0 vec_dot kernel (see src/ggml-quants.c: dequantize_row_tq2_0
// upstream, as of GGML v0.15.3). No GGML headers are used here -- this file
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
// TQ2_0's quantize/dequantize kernels are no longer defined here -- they
// are the "tq2_0_quantize"/"tq2_0_dequantize" GENERATOR_ARGS instantiations
// of the generic, reusable Approximation-based
// lookup_table_quantize/lookup_table_dequantize generators in
// lookup_table_quant_generators.cpp (see quant_components.h's
// LookupTableQuantize/TwoBitPack; quantize still delegates to GGML's own
// reference via a Halide extern stage there too -- see
// ggml_extern_quantize.cpp for why, kept consistent with TQ1_0's treatment
// even though TQ2_0's quantizer is simpler). Only vec_dot, which still
// hand-rolls its own dequantize math, is unscheduled beyond the minimum
// Halide requires for legality -- scheduling for performance is a later
// step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kQsOffset = 0;
constexpr int kDOffset = kQK_K / 4;        // 64
constexpr int kBlockBytes = kDOffset + 2;  // 66

// vec_dot(TQ2_0, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class TQ2_0VecDotGenerator : public Generator<TQ2_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr half = gi / 128;
            Expr local = gi % 128;
            Expr l = local / 32;
            Expr m = local % 32;
            Expr byte_idx = kQsOffset + half * 32 + m;
            Expr byte_val = x_blocks_(byte_idx, i);
            Expr q = cast<int32_t>((byte_val >> (l * 2)) & 3);
            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
            return cast<float>(q - 1) * d;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TQ2_0VecDotGenerator, tq2_0_vec_dot)
