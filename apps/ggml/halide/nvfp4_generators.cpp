// From-scratch Halide reimplementation of GGML's NVFP4 dequantize kernel
// (see src/ggml-quants.c: dequantize_row_nvfp4 upstream, as of GGML
// v0.15.3). No GGML headers are used by the dequantize generator -- it
// encodes its own understanding of the 36-byte block_nvfp4 layout (a
// 64-element block, 4 sub-blocks of 16 elements each):
//
//   byte 0-3:   d[4] -- one UE4M3 (unsigned 4-exponent/3-mantissa) scale
//               byte per 16-element sub-block
//   byte 4-35:  qs[32] -- 4 bits per element (2 elements per byte), each a
//               codebook index into the same fixed 16-value kvalues_mxfp4
//               table MXFP4 uses (NVFP4 is MXFP4 with finer-grained scales)
//
// GGML converts UE4M3 to float via ggml_ue4m3_to_fp32, which for finite,
// non-subnormal inputs is `(1 + man/8) * 2^(exp-7) * 0.5` -- reproduced here
// via ordinary floating-point arithmetic (exact power-of-two scaling; the
// dequantize benchmark tolerates 1% relative error, so this isn't held to
// the same bit-exactness bar as quantize output).
//
// Quantize is NOT reimplemented here: GGML's reference quantizer's UE4M3
// encoding involves exponent/mantissa rounding not guaranteed to be
// bit-reproducible against a from-scratch implementation -- see
// ggml_extern_quantize.cpp for why and how this Func instead calls out to
// GGML's own reference via a Halide extern stage.
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 64;
constexpr int kSubSize = 16;
constexpr int kNumSubs = kQK / kSubSize;  // 4
constexpr int kDOffset = 0;
constexpr int kQsOffset = kDOffset + kNumSubs;    // 4
constexpr int kBlockBytes = kQsOffset + kQK / 2;  // 36

// kvalues_mxfp4 = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12}
Expr lookup_mxfp4(Expr idx) {
    return select(idx == 0, 0, idx == 1, 1, idx == 2, 2, idx == 3, 3,
                  idx == 4, 4, idx == 5, 6, idx == 6, 8, idx == 7, 12,
                  idx == 8, 0, idx == 9, -1, idx == 10, -2, idx == 11, -3,
                  idx == 12, -4, idx == 13, -6, idx == 14, -8, -12);
}

class NVFP4DequantizeGenerator : public Generator<NVFP4DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK;
        Expr gi = x % kQK;
        Expr sub = gi / kSubSize;
        Expr local = gi % kSubSize;

        Expr j = local % (kSubSize / 2);  // 0..7
        Expr is_low = local < (kSubSize / 2);

        Expr qs_byte_idx = sub * (kSubSize / 2) + j;
        Expr byte = blocks_(kQsOffset + qs_byte_idx, i);
        Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));
        Expr val = lookup_mxfp4(nibble);

        // ggml_ue4m3_to_fp32: ordinary floating-point reconstruction
        // (dequantize is tolerance-checked, not bit-exact).
        Expr ue = blocks_(kDOffset + sub, i);                      // codespell:ignore ue
        Expr is_zero = (ue == 0) || (ue == 0x7f);                  // codespell:ignore ue
        Expr exp_ = cast<int32_t>(cast<uint32_t>(ue) >> 3) & 0xf;  // codespell:ignore ue
        Expr man_ = cast<int32_t>(ue) & 0x7;                       // codespell:ignore ue
        Expr raw = select(exp_ == 0,
                          cast<float>(man_) / 512.0f,
                          (1.0f + cast<float>(man_) / 8.0f) * pow(2.0f, cast<float>(exp_ - 7)));
        Expr d = select(is_zero, 0.0f, raw * 0.5f);

        y_(x) = cast<float>(val) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class NVFP4QuantizeGenerator : public Generator<NVFP4QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("nvfp4_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

// vec_dot(NVFP4, Q8_0): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
// NVFP4's own block size (64) differs from Q8_0's (32) -- no issue, since
// each side's value function independently derives its own block/lane
// indices from the shared flat element index.
class NVFP4VecDotGenerator : public Generator<NVFP4VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_0 activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK;
            Expr gi = x % kQK;
            Expr sub = gi / kSubSize;
            Expr local = gi % kSubSize;
            Expr j = local % (kSubSize / 2);
            Expr is_low = local < (kSubSize / 2);
            Expr qs_byte_idx = sub * (kSubSize / 2) + j;
            Expr byte = x_blocks_(kQsOffset + qs_byte_idx, i);
            Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));
            Expr val = lookup_mxfp4(nibble);
            Expr ue = x_blocks_(kDOffset + sub, i);                    // codespell:ignore ue
            Expr is_zero = (ue == 0) || (ue == 0x7f);                  // codespell:ignore ue
            Expr exp_ = cast<int32_t>(cast<uint32_t>(ue) >> 3) & 0xf;  // codespell:ignore ue
            Expr man_ = cast<int32_t>(ue) & 0x7;                       // codespell:ignore ue
            Expr raw = select(exp_ == 0,
                              cast<float>(man_) / 512.0f,
                              (1.0f + cast<float>(man_) / 8.0f) * pow(2.0f, cast<float>(exp_ - 7)));
            Expr d = select(is_zero, 0.0f, raw * 0.5f);
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

HALIDE_REGISTER_GENERATOR(NVFP4DequantizeGenerator, nvfp4_dequantize)
HALIDE_REGISTER_GENERATOR(NVFP4QuantizeGenerator, nvfp4_quantize)
HALIDE_REGISTER_GENERATOR(NVFP4VecDotGenerator, nvfp4_vec_dot)
