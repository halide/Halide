// From-scratch Halide reimplementation of GGML's IQ1_M dequantize kernel
// (see src/ggml-quants.c: dequantize_row_iq1_m upstream, as of GGML
// v0.15.3). Uses GGML's published iq1s_grid codebook constants verbatim
// (see iq_grids_data.h) -- fixed lookup-table data, not derived logic.
//
// The 56-byte block_iq1_m layout (a 256-element superblock, 8 groups of 32
// elements each, "ib" below) -- notably, unlike every other type here,
// there is NO separate delta field:
//
//   byte 0-31:  qs[32] -- one grid-index low byte per l2 (4 l2's per group)
//   byte 32-47: qh[16] -- 2 bytes per group; each byte supplies a high-3-bit
//               grid index extension (two different nibble shifts) and a
//               sign-delta-select bit for two of the group's four l2's
//   byte 48-55: scales[8], reinterpreted as 4 little-endian uint16 words
//               "sc[0..3]": the block's SHARED fp16 delta is bit-gathered
//               from the TOP 4 bits of all 4 words (4 nibbles -> 16 bits),
//               while each word's LOW 12 bits hold two 3-bit per-group
//               scale fields (one for each of the two groups whose ib
//               shares that word)
//
// Same signed-codebook convention as IQ1_S (see iq1_s_generators.cpp):
// iq1s_grid entries are used directly as signed bytes, with a small
// +/-IQ1S_DELTA offset applied per l2 rather than an external sign flip.
//
// GGML has no public from_float_ref for this type (an importance-matrix-
// only codebook quantizer -- see providers/ggml_internal_abi.h's
// quantize_iq1_m doc comment), so there is no quantize generator here.
//
// This is intentionally unscheduled -- scheduling for performance is a
// later step.

#include "Halide.h"

#include "activation_dequant.h"
#include "iq_grids_data.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr float kIQ1S_DELTA = 0.125f;
constexpr int kQsOffset = 0;
constexpr int kQhOffset = kQK_K / 8;                     // 32
constexpr int kScalesOffset = kQhOffset + kQK_K / 16;    // 48
constexpr int kBlockBytes = kScalesOffset + kQK_K / 32;  // 56

class IQ1_MDequantizeGenerator : public Generator<IQ1_MDequantizeGenerator> {
public:
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Input<Buffer<uint8_t, 2>> blocks_{"blocks"};
    Output<Buffer<float, 1>> y_{"y"};

    void generate() {
        Buffer<uint64_t, 1> grid(2048, "iq1s_grid");
        for (int idx = 0; idx < 2048; idx++)
            grid(idx) = iq_grids::iq1s_grid[idx];

        Var x("x");

        Expr i = x / kQK_K;
        Expr gi = x % kQK_K;
        Expr ib = gi / 32;  // 0..7, matches qh/scale granularity
        Expr local = gi % 32;
        Expr l2 = local / 8;  // 0..3
        Expr j = local % 8;   // 0..7

        // sc[0..3]: the 4 little-endian uint16 words of the scales field.
        auto sc_word = [&](Expr k) {
            Expr lo = cast<uint32_t>(blocks_(kScalesOffset + k * 2 + 0, i));
            Expr hi = cast<uint32_t>(blocks_(kScalesOffset + k * 2 + 1, i));
            return lo | (hi << 8);
        };
        Expr sc0 = sc_word(0), sc1 = sc_word(1), sc2 = sc_word(2), sc3 = sc_word(3);
        Expr d_bits = (sc0 >> 12) | ((sc1 >> 8) & 0xf0) | ((sc2 >> 4) & 0xf00) | (sc3 & 0xf000);
        Expr d = cast<float>(reinterpret<float16_t>(cast<uint16_t>(d_bits)));

        Expr qh_half = l2 / 2;  // 0 or 1: which of this group's 2 qh bytes
        Expr parity = l2 % 2;   // 0 or 1: which nibble-shift/delta-bit within that byte

        Expr qh_byte = cast<uint32_t>(blocks_(kQhOffset + ib * 2 + qh_half, i));
        Expr qs_byte = blocks_(kQsOffset + ib * 4 + l2, i);
        // A *variable-amount* left shift feeding a Buffer index defeats
        // Halide's bounds inference even when masked afterward (unlike a
        // constant-amount shift) -- branch over parity's 2 possible values
        // so each arm's shift amount is a compile-time constant.
        Expr extra_bits = select(parity == 0, (qh_byte << 8) & 0x700, (qh_byte << 4) & 0x700);
        // See iq2_s_generators.cpp's grid_idx comment: '+' instead of the
        // algebraically-equivalent '|' for reliable bounds inference, plus
        // a narrow-type cast for a hard type-level bound before the lookup.
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + extra_bits)), 0, 2047);

        Expr delta_mask = select(parity == 0, cast<uint32_t>(0x08), cast<uint32_t>(0x80));
        Expr delta = select((qh_byte & delta_mask) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

        // sc_idx = ib/2 (which word), shift = 6*(ib%2) + 3*qh_half (which
        // 3-bit scale field within that word).
        Expr sc_idx = ib / 2;
        Expr sc_word_val = select(sc_idx == 0, sc0, select(sc_idx == 1, sc1, select(sc_idx == 2, sc2, sc3)));
        Expr shift = (ib % 2) * 6 + qh_half * 3;
        Expr scale3 = (sc_word_val >> cast<uint32_t>(shift)) & 7;
        Expr dl = d * cast<float>(2 * scale3 + 1);

        Expr grid_val = grid(grid_idx);
        Expr grid_byte_bits = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr grid_signed = reinterpret<int8_t>(grid_byte_bits);

        y_(x) = dl * (cast<float>(grid_signed) + delta);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        y_.dim(0).set_min(0);
    }
};

// vec_dot(IQ1_M, Q8_K): plain dequantize-then-multiply-and-sum, mirroring
// Q4_0's vec_dot generator (see q4_0_generators.cpp for the rationale).
class IQ1_MVecDotGenerator : public Generator<IQ1_MVecDotGenerator> {
public:
    Input<Buffer<uint8_t, 2>> x_blocks_{"x_blocks"};
    Input<Buffer<uint8_t, 2>> y_blocks_{"y_blocks"};  // Q8_K activation format
    Output<Buffer<float, 0>> result_{"result"};

    void generate() {
        Buffer<uint64_t, 1> grid(2048, "iq1s_grid");
        for (int idx = 0; idx < 2048; idx++)
            grid(idx) = iq_grids::iq1s_grid[idx];

        RDom r(0, x_blocks_.dim(1).extent() * kQK_K, "r");

        auto x_val = [&](Expr x) -> Expr {
            Expr i = x / kQK_K;
            Expr gi = x % kQK_K;
            Expr ib = gi / 32;
            Expr local = gi % 32;
            Expr l2 = local / 8;
            Expr j = local % 8;

            auto sc_word = [&](Expr k) {
                Expr lo = cast<uint32_t>(x_blocks_(kScalesOffset + k * 2 + 0, i));
                Expr hi = cast<uint32_t>(x_blocks_(kScalesOffset + k * 2 + 1, i));
                return lo | (hi << 8);
            };
            Expr sc0 = sc_word(0), sc1 = sc_word(1), sc2 = sc_word(2), sc3 = sc_word(3);
            Expr d_bits = (sc0 >> 12) | ((sc1 >> 8) & 0xf0) | ((sc2 >> 4) & 0xf00) | (sc3 & 0xf000);
            Expr d = cast<float>(reinterpret<float16_t>(cast<uint16_t>(d_bits)));

            Expr qh_half = l2 / 2;
            Expr parity = l2 % 2;

            Expr qh_byte = cast<uint32_t>(x_blocks_(kQhOffset + ib * 2 + qh_half, i));
            Expr qs_byte = x_blocks_(kQsOffset + ib * 4 + l2, i);
            Expr extra_bits = select(parity == 0, (qh_byte << 8) & 0x700, (qh_byte << 4) & 0x700);
            Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + extra_bits)), 0, 2047);

            Expr delta_mask = select(parity == 0, cast<uint32_t>(0x08), cast<uint32_t>(0x80));
            Expr delta = select((qh_byte & delta_mask) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

            Expr sc_idx = ib / 2;
            Expr sc_word_val = select(sc_idx == 0, sc0, select(sc_idx == 1, sc1, select(sc_idx == 2, sc2, sc3)));
            Expr shift = (ib % 2) * 6 + qh_half * 3;
            Expr scale3 = (sc_word_val >> cast<uint32_t>(shift)) & 7;
            Expr dl = d * cast<float>(2 * scale3 + 1);

            Expr grid_val = grid(grid_idx);
            Expr grid_byte_bits = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
            Expr grid_signed = reinterpret<int8_t>(grid_byte_bits);

            return dl * (cast<float>(grid_signed) + delta);
        };

        result_() = sum(x_val(r) * ggml_halide::q8_k_value(y_blocks_, r));

        x_blocks_.dim(0).set_bounds(0, kBlockBytes);
        x_blocks_.dim(1).set_min(0);
        y_blocks_.dim(0).set_bounds(0, 292);
        y_blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(IQ1_MDequantizeGenerator, iq1_m_dequantize)
HALIDE_REGISTER_GENERATOR(IQ1_MVecDotGenerator, iq1_m_vec_dot)
