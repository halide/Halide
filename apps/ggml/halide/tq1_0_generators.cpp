// From-scratch Halide reimplementation of GGML's TQ1_0 dequantize kernel
// (see src/ggml-quants.c: dequantize_row_tq1_0 upstream, as of GGML
// v0.15.3). No GGML headers are used by the dequantize generator -- it
// encodes its own understanding of the 54-byte block_tq1_0 layout (a
// 256-element ternary superblock):
//
//   byte 0-47:  qs[48] -- 5 ternary digits packed per byte (3^5=243<256):
//               covers the first 240 elements
//   byte 48-51: qh[4]  -- 4 ternary digits packed per byte (using only the
//               top 4 of the 5 available trit slots): covers the last 16
//               elements
//   byte 52-53: fp16 delta 'd'
//
// GGML packs 5 base-3 digits (each in {0,1,2}, representing {-1,0,1}) into
// one byte via q = round(digit_number * 256 / 243) (a ceiling division so
// the 243 possible 5-digit values spread roughly uniformly over a byte).
// Decoding digit n back out of a packed byte multiplies by pow3[n], TRUNCATES
// to 8 bits (mod 256 -- this truncation is load-bearing, not incidental),
// then recovers the digit from the top bits of a *3 rescale. This is
// reproduced exactly (integer-only, no floating-point rounding involved in
// digit extraction).
//
// Quantize is NOT reimplemented here: while its scale itself is closed-form
// (mean(|x|), not an iterative search), assembling the base-3 packed bytes
// involves several fixed-digit accumulation-and-rescale steps that are
// straightforward in C but comparatively fiddly to unroll in Halide's
// functional style -- deferred for now, see ggml_extern_quantize.cpp for
// how this Func instead calls out to GGML's own reference via a Halide
// extern stage.
//
// The dequantize generator is intentionally unscheduled -- scheduling for
// performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kQsOffset = 0;
constexpr int kQhOffset = 48;
constexpr int kDOffset = 52;
constexpr int kBlockBytes = 54;

class TQ1_0DequantizeGenerator : public Generator<TQ1_0DequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;

        // Section A: qs bytes [0, 32), 5 digits each -> elements [0, 160).
        Expr n_a = gi / 32;
        Expr m_a = gi % 32;
        Expr byte_a = kQsOffset + m_a;

        // Section B: qs bytes [32, 48), 5 digits each -> elements [160, 240).
        Expr local_b = gi - 160;
        Expr n_b = local_b / 16;
        Expr m_b = local_b % 16;
        Expr byte_b = kQsOffset + 32 + m_b;

        // Section C: qh bytes [0, 4), only 4 (of 5) digit slots used ->
        // elements [240, 256).
        Expr local_c = gi - 240;
        Expr n_c = local_c / 4;
        Expr j_c = local_c % 4;
        Expr byte_c = kQhOffset + j_c;

        Expr n = select(gi < 160, n_a, select(gi < 240, n_b, n_c));
        Expr byte_abs = select(gi < 160, byte_a, select(gi < 240, byte_b, byte_c));

        Expr byte_val = blocks_(byte_abs, i);
        Expr p3 = select(n == 0, 1, n == 1, 3, n == 2, 9, n == 3, 27, n == 4, 81, 243);

        // q = (uint8_t)(byte_val * p3) -- the truncation to 8 bits is
        // exactly what GGML's reference does and is load-bearing (this is
        // modular-arithmetic digit extraction, not incidental overflow).
        Expr q_trunc = cast<uint8_t>(cast<uint32_t>(byte_val) * cast<uint32_t>(p3));
        Expr xi = cast<int32_t>((cast<uint16_t>(q_trunc) * 3) >> 8);

        Expr d_lo = cast<uint16_t>(blocks_(kDOffset + 0, i));
        Expr d_hi = cast<uint16_t>(blocks_(kDOffset + 1, i));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

        y_(x) = cast<float>(xi - 1) * d;

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

class TQ1_0QuantizeGenerator : public Generator<TQ1_0QuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        std::vector<ExternFuncArgument> args = {Func(x_)};
        blocks_.define_extern("tq1_0_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
    }
};

// vec_dot(TQ1_0, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class TQ1_0VecDotGenerator : public Generator<TQ1_0VecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;

            Expr n_a = gi / 32;
            Expr m_a = gi % 32;
            Expr byte_a = kQsOffset + m_a;

            Expr local_b = gi - 160;
            Expr n_b = local_b / 16;
            Expr m_b = local_b % 16;
            Expr byte_b = kQsOffset + 32 + m_b;

            Expr local_c = gi - 240;
            Expr n_c = local_c / 4;
            Expr j_c = local_c % 4;
            Expr byte_c = kQhOffset + j_c;

            Expr n = select(gi < 160, n_a, select(gi < 240, n_b, n_c));
            Expr byte_abs = select(gi < 160, byte_a, select(gi < 240, byte_b, byte_c));

            Expr byte_val = x_blocks_(byte_abs, i);
            Expr p3 = select(n == 0, 1, n == 1, 3, n == 2, 9, n == 3, 27, n == 4, 81, 243);

            Expr q_trunc = cast<uint8_t>(cast<uint32_t>(byte_val) * cast<uint32_t>(p3));
            Expr xi = cast<int32_t>((cast<uint16_t>(q_trunc) * 3) >> 8);

            Expr d_lo = cast<uint16_t>(x_blocks_(kDOffset + 0, i));
            Expr d_hi = cast<uint16_t>(x_blocks_(kDOffset + 1, i));
            Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

            return cast<float>(xi - 1) * d;
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(TQ1_0DequantizeGenerator, tq1_0_dequantize)
HALIDE_REGISTER_GENERATOR(TQ1_0QuantizeGenerator, tq1_0_quantize)
HALIDE_REGISTER_GENERATOR(TQ1_0VecDotGenerator, tq1_0_vec_dot)
