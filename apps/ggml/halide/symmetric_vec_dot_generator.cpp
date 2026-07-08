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
        switch (w_kind.value()) {
        case WKind::Symmetric:
            wc = make_symmetric_block_codec(wbs, w_qmax, w_rounding, w_anchor, w_code_bits);
            wb = 2 + (w_code_bits == 4 ? wbs / 2 : (w_code_bits == 1 ? wbs / 8 : wbs));
            sched = ScheduleKind::SDOT;
            break;
        case WKind::Affine:
            wc = make_affine_block_codec(wbs, w_levels, w_affine_rounding, w_code_bits);
            wb = 2 + 2 + (w_code_bits == 4 ? wbs / 2 : wbs);
            sched = ScheduleKind::Float;
            break;
        case WKind::Symmetric5Bit:
            wc = make_symmetric_5bit_block_codec(wbs, w_qmax);
            wb = 2 + 4 + wbs / 2;
            sched = ScheduleKind::SDOT;
            break;
        case WKind::Affine5Bit:
            wc = make_affine_5bit_block_codec(wbs, w_levels, w_affine_rounding);
            wb = 2 + 2 + 4 + wbs / 2;
            sched = ScheduleKind::Float;
            break;
        }

        // Q8_0/Q8_1 activations are 32-element blocks. Build the codec at that
        // natural block size, then Reblock to the weight's block size (a no-op
        // when they already match, e.g. Q4_0/Q8_0); the byte width stays the
        // natural-block width since y_blocks is stored at 32-element blocks.
        const int a_nat = 32;
        std::unique_ptr<Halide::Approximation> ac;
        int ab;
        switch (a_kind.value()) {
        case AKind::Q8_0:
            ac = make_symmetric_block_codec(a_nat, a_qmax, RoundingMode::Nearest, ScaleAnchor::AbsMax, 8);
            ab = 2 + a_nat;
            break;
        case AKind::Q8_1:
            ac = make_symmetric_byte_sum_block_codec(a_nat, a_qmax);
            ab = 2 + 2 + a_nat;
            break;
        }
        ac = reblock_activation(std::move(ac), a_nat, wbs);

        return {std::move(wc), wb, std::move(ac), ab, wbs, sched};
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SymmetricVecDotGenerator, symmetric_vec_dot)
