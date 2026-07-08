// Generic, family-driven vec_dot for the codebook/grid formats, the vec_dot
// counterpart of lookup_table_quant_generators.cpp's LookupTableCodecGenerator
// (same family set). "iq4_nl_vec_dot" is not a distinct C++ class -- it's a
// PARAMS family=iq4_nl instantiation of this one generator, registered in
// CMakeLists.txt. Weight and activation are both block-indexed codecs from
// quant_components.h; VecDotGeneratorBase splices them via approximate_by/
// compute_offline (see vec_dot_generator_base.h).

#include "Halide.h"

#include "quant_components.h"
#include "vec_dot_generator_base.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

enum class Family { IQ4_NL,
                    MXFP4,
                    TQ2_0,
                    TQ1_0,
                    NVFP4,
                    IQ2_S,
                    IQ3_XXS,
                    IQ3_S,
                    IQ4_XS,
                    IQ2_XS,
                    IQ2_XXS,
                    IQ1_S,
                    IQ1_M };

class LookupTableVecDotGenerator : public VecDotGeneratorBase<LookupTableVecDotGenerator> {
public:
    GeneratorParam<Family> family{
        "family",
        Family::IQ4_NL,
        {{"iq4_nl", Family::IQ4_NL},
         {"mxfp4", Family::MXFP4},
         {"tq2_0", Family::TQ2_0},
         {"tq1_0", Family::TQ1_0},
         {"nvfp4", Family::NVFP4},
         {"iq2_s", Family::IQ2_S},
         {"iq3_xxs", Family::IQ3_XXS},
         {"iq3_s", Family::IQ3_S},
         {"iq4_xs", Family::IQ4_XS},
         {"iq2_xs", Family::IQ2_XS},
         {"iq2_xxs", Family::IQ2_XXS},
         {"iq1_s", Family::IQ1_S},
         {"iq1_m", Family::IQ1_M}}};

    // Q8_0 activation codec (block_q8_0 = {fp16 d; qs[32]} = 34 bytes).
    static std::unique_ptr<Halide::Approximation> q8_0_codec() {
        return make_symmetric_block_codec(32, 127, RoundingMode::Nearest, ScaleAnchor::AbsMax, 8);
    }
    // Q8_K activation codec (block_q8_K = {float d; qs[256]; bsums[16]} = 292 bytes).
    static std::unique_ptr<Halide::Approximation> q8_k_codec() {
        return make_q8_k_codec(256, 127);
    }

    VecDotSpec build_vec_dot() const {
        switch (family.value()) {
        case Family::IQ4_NL:
            // 4-bit codebook, single fp16 scale x Q8_0: single per-block scale
            // and int8 codebook values -> SDOT-eligible.
            return {make_iq4_nl_codec(), 18, q8_0_codec(), 34, 32, ScheduleKind::SDOT};
        case Family::MXFP4:
            // Same single-scale codebook shape as IQ4_NL (E8M0 scale) x Q8_0.
            return {make_mxfp4_codec(), 17, q8_0_codec(), 34, 32, ScheduleKind::SDOT};
        case Family::NVFP4:
            // 64-element block (4 sub-scales) x Q8_0 (32-block): the activation
            // is Reblocked 32 -> 64 so both share the weight's block. Sub-block
            // scales -> Float.
            return {make_nvfp4_codec(), 36, reblock_activation(q8_0_codec(), 32, 64), 34, 64, ScheduleKind::Float};
        // TQ1_0/TQ2_0 x Q8_K, IQ4_XS x Q8_K: single fp16 scale (TQ) or two-level
        // scale (IQ4_XS) -> Float schedule for now.
        case Family::TQ2_0:
            return {make_tq2_0_codec(), 66, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::TQ1_0:
            return {make_tq1_0_codec(), 54, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ4_XS:
            return {make_iq4_xs_codec(), 136, q8_k_codec(), 292, 256, ScheduleKind::Float};
        // Grid formats x Q8_K: per-group scale + sign -> Float schedule. The
        // block-indexed codec collapses the leaf's {8,4,8} output to (kk, blk).
        case Family::IQ2_S:
            return {make_iq2_s_codec(), 82, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ3_XXS:
            return {make_iq3_xxs_codec(), 98, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ3_S:
            return {make_iq3_s_codec(), 110, q8_k_codec(), 292, 256, ScheduleKind::Float};
        // Importance-matrix-only formats (SeveredEncode weight scheme) x Q8_K.
        case Family::IQ2_XS:
            return {make_iq2_xs_codec(), 74, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ2_XXS:
            return {make_iq2_xxs_codec(), 66, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ1_S:
            return {make_iq1_s_codec(), 50, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::IQ1_M:
            return {make_iq1_m_codec(), 56, q8_k_codec(), 292, 256, ScheduleKind::Float};
        default:
            break;
        }
        _halide_internal_error << "LookupTableVecDotGenerator: family not yet converted\n";
        return {};
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LookupTableVecDotGenerator, lookup_table_vec_dot)
