// Generic, family-driven vec_dot for the symmetric/affine per-block formats,
// the vec_dot counterpart of symmetric_quant_generators.cpp's
// SymmetricCodecGenerator. "q4_0_vec_dot"/"q4_1_vec_dot"/... are PARAMS
// instantiations of this one generator (registered in CMakeLists.txt), not
// per-format C++ classes. Weight and activation are both block-indexed codecs
// from quant_components.h; VecDotGeneratorBase splices them via approximate_by/
// compute_offline (see vec_dot_generator_base.h) -- generate() never calls
// Approximation::encode()/decode() directly.
//
// The weight is one of the symmetric-family kinds (symmetric / affine /
// symmetric_5bit / affine_5bit); the activation is Q8_0 or Q8_1. Single-scale
// symmetric weights x Q8_0 reach an SDOT Int(32) inner dot; affine (+min) and
// mismatched-block pairings (Q1_0 block 128 x Q8_0 block 32) fall back to a
// Float reduction.

#include "Halide.h"

#include "quant_components.h"
#include "vec_dot_generator_base.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

enum class WKind { Symmetric,
                   Affine,
                   Symmetric5Bit,
                   Affine5Bit };
enum class AKind { Q8_0,
                   Q8_1 };

class SymmetricVecDotGenerator : public VecDotGeneratorBase<SymmetricVecDotGenerator> {
public:
    GeneratorParam<int> block_size{"block_size", 32};

    GeneratorParam<WKind> w_kind{
        "w_kind",
        WKind::Symmetric,
        {{"symmetric", WKind::Symmetric},
         {"affine", WKind::Affine},
         {"symmetric_5bit", WKind::Symmetric5Bit},
         {"affine_5bit", WKind::Affine5Bit}}};
    GeneratorParam<AKind> a_kind{
        "a_kind",
        AKind::Q8_0,
        {{"q8_0", AKind::Q8_0},
         {"q8_1", AKind::Q8_1}}};

    // symmetric / symmetric_5bit weight params
    GeneratorParam<int> w_qmax{"w_qmax", 8};
    GeneratorParam<int> w_code_bits{"w_code_bits", 4};
    GeneratorParam<RoundingMode> w_rounding{
        "w_rounding",
        RoundingMode::TruncateHalfUpWithOffset,
        {{"nearest", RoundingMode::Nearest},
         {"truncate_half_up_with_offset", RoundingMode::TruncateHalfUpWithOffset},
         {"sign_only", RoundingMode::SignOnly}}};
    GeneratorParam<ScaleAnchor> w_anchor{
        "w_anchor",
        ScaleAnchor::ExtremeSignedValue,
        {{"abs_max", ScaleAnchor::AbsMax},
         {"extreme_signed", ScaleAnchor::ExtremeSignedValue},
         {"mean_abs", ScaleAnchor::MeanAbs}}};

    // affine / affine_5bit weight params
    GeneratorParam<int> w_levels{"w_levels", 15};
    GeneratorParam<AffineRounding> w_affine_rounding{
        "w_affine_rounding",
        AffineRounding::ClampedInt8,
        {{"clamped_int8", AffineRounding::ClampedInt8},
         {"unclamped_uint8", AffineRounding::UnclampedUint8}}};

    // activation param (Q8_0/Q8_1 are always 8-bit int8 codes)
    GeneratorParam<int> a_qmax{"a_qmax", 127};

    VecDotSpec build_vec_dot() const {
        int wbs = block_size;

        std::unique_ptr<Halide::Approximation> wc;
        int wb;
        ScheduleKind sched;
        bool distribute = false;   // set for the affine (offset-carrying) weights
        Halide::Type weight_type;  // set -> weight blocks are a 1-D Type::Struct buffer
        Halide::ApproximationStageKey reconstructed_codes_stage, packed_high_word_stage;
        switch (w_kind.value()) {
        case WKind::Symmetric: {
            // Struct-typed weight blocks (`{fp16 d; uint8 qs[...]}`); SDOT still
            // works because the base header's deep inline flattens the struct
            // decode's dequantizer just like the byte path's.
            SchemeAndBytes sb = make_symmetric_block_scheme(wbs, w_qmax, w_rounding, w_anchor, w_code_bits,
                                                            Layout::BlockIndexed, /*struct_layout=*/true);
            wc = std::move(sb.scheme);
            weight_type = sb.block_type;
            wb = 2 + (w_code_bits == 4 ? wbs / 2 : (w_code_bits == 1 ? wbs / 8 : wbs));
            // 1-bit (Q1_0) stays Float: change_type(Int(32)) can't prove its
            // deep-inlined per-term range fits Int(32) (its 128-wide block trips
            // the overflow check), and it's a niche format. Q4_0/Q8_0 -> SDOT.
            sched = w_code_bits == 1 ? ScheduleKind::Float : ScheduleKind::SDOT;
            break;
        }
        case WKind::Affine:
            wc = make_affine_block_scheme(wbs, w_levels, w_affine_rounding, w_code_bits, Layout::BlockIndexed).scheme;
            wb = 2 + 2 + (w_code_bits == 4 ? wbs / 2 : wbs);
            // The affine decode is (d*code + m), so the per-block product is not
            // a single scaled term. hoist_invariants() multiplies it out into
            //   d*d_act * sum(code*act)  +  m*d_act * sum(act)
            // and gives each term its own accumulator, which is ggml's own
            // decomposition -- both inner sums are integer, so both reach SDOT.
            sched = ScheduleKind::SDOT;
            distribute = true;
            break;
        case WKind::Symmetric5Bit: {
            SchemeAndBytes sb = make_symmetric_5bit_block_scheme(wbs, w_qmax, Layout::BlockIndexed);
            wc = std::move(sb.scheme);
            weight_type = sb.block_type;
            reconstructed_codes_stage = sb.reconstructed_codes_stage;
            packed_high_word_stage = sb.packed_high_word_stage;
            wb = 2 + 4 + wbs / 2;
            // Q5_0's 5-bit code is assembled via CombineBits (nibble |
            // (high_bit << 4)); that reconstruction is all inside the (r.x-
            // dependent) codes leaf, so the per-block scale is still a top-level
            // invariant factor once the decode chain is fully inlined -- SDOT
            // works the same as Q4_0 (see the base header's deep-inline SDOT).
            sched = ScheduleKind::SDOT;
            break;
        }
        case WKind::Affine5Bit: {
            SchemeAndBytes sb = make_affine_5bit_block_scheme(wbs, w_levels, w_affine_rounding, Layout::BlockIndexed);
            wc = std::move(sb.scheme);
            weight_type = sb.block_type;
            wb = 2 + 2 + 4 + wbs / 2;
            // Same offset-carrying decode as the 4-bit affine case above; the
            // 5-bit code reconstruction stays inside the codes leaf, so the
            // multiplied-out terms are integer just the same.
            sched = ScheduleKind::SDOT;
            distribute = true;
            break;
        }
        }

        // Q8_0/Q8_1 activations are 32-element blocks. Build the codec at that
        // natural block size, then Reblock to the weight's block size (a no-op
        // when they already match, e.g. Q4_0/Q8_0); the byte width stays the
        // natural-block width since y_blocks is stored at 32-element blocks.
        const int a_nat = 32;
        std::unique_ptr<Halide::Approximation> ac;
        int ab;
        bool act_has_block_sums = false;
        switch (a_kind.value()) {
        case AKind::Q8_0:
            ac = make_symmetric_block_scheme(a_nat, a_qmax, RoundingMode::Nearest, ScaleAnchor::AbsMax, 8, Layout::BlockIndexed).scheme;
            ab = 2 + a_nat;
            break;
        case AKind::Q8_1: {
            SchemeAndBytes sb = make_symmetric_byte_sum_block_scheme(a_nat, a_qmax, Layout::BlockIndexed);
            ac = std::move(sb.scheme);
            ab = 2 + 2 + a_nat;
            // Q8_1 stores its per-block scaled code sum (`s`); the affine vec_dot
            // severs the offset term straight to it. Only meaningful when the
            // activation block matches the weight block (a_nat == wbs, i.e. no
            // Reblock) -- true for the q4_1/q5_1 pairings.
            act_has_block_sums = sb.has_block_sums && a_nat == wbs;
            break;
        }
        }
        ac = reblock_activation(std::move(ac), a_nat, wbs);

        // Activation stays on the byte path for now (Q8_0/Q8_1 are shared across
        // many weight formats, and the Reblock relayout is byte-based); only the
        // weight operand is struct-typed here.
        const bool five_bit = w_kind.value() == WKind::Symmetric5Bit || w_kind.value() == WKind::Affine5Bit;
        return {std::move(wc), wb, std::move(ac), ab, wbs, sched, distribute, weight_type, Halide::Type{}, act_has_block_sums,
                five_bit ? 2 : 4, reconstructed_codes_stage, packed_high_word_stage};
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SymmetricVecDotGenerator, symmetric_vec_dot)
