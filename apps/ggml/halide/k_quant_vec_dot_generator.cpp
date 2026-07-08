// Generic, family-driven vec_dot for the K-quant super-block formats, the
// vec_dot counterpart of k_quant_generators.cpp's KQuantCodecGenerator (same
// family set). "q4_k_vec_dot" is a PARAMS family=q4_k instantiation of this
// one generator. Weight is a block-indexed K-quant codec, activation is the
// block-indexed Q8_K codec; VecDotGeneratorBase splices both via
// approximate_by/compute_offline. K-quant decode is a two-level (sub-block)
// scale, so the per-block scale is not single-invariant -> Float schedule.

#include "Halide.h"

#include "quant_components.h"
#include "vec_dot_generator_base.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

enum class Family { Q2_K,
                    Q3_K,
                    Q4_K,
                    Q5_K,
                    Q6_K };

class KQuantVecDotGenerator : public VecDotGeneratorBase<KQuantVecDotGenerator> {
public:
    GeneratorParam<Family> family{
        "family",
        Family::Q4_K,
        {{"q2_k", Family::Q2_K},
         {"q3_k", Family::Q3_K},
         {"q4_k", Family::Q4_K},
         {"q5_k", Family::Q5_K},
         {"q6_k", Family::Q6_K}}};

    // Q8_K activation codec (block_q8_K = {float d; qs[256]; bsums[16]} = 292 bytes).
    static std::unique_ptr<Halide::Approximation> q8_k_codec() {
        return make_q8_k_scheme(256, 127, Layout::BlockIndexed).scheme;
    }

    VecDotSpec build_vec_dot() const {
        // All K-quants: 256-element super-block, Q8_K activation, two-level
        // scale -> Float schedule.
        switch (family.value()) {
        case Family::Q2_K:
            return {make_q2_k_scheme(Layout::BlockIndexed).scheme, 84, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::Q3_K:
            return {make_q3_k_scheme(Layout::BlockIndexed).scheme, 110, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::Q4_K:
            return {make_q4_k_scheme(Layout::BlockIndexed).scheme, 144, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::Q5_K:
            return {make_q5_k_scheme(Layout::BlockIndexed).scheme, 176, q8_k_codec(), 292, 256, ScheduleKind::Float};
        case Family::Q6_K:
            return {make_q6_k_scheme(Layout::BlockIndexed).scheme, 210, q8_k_codec(), 292, 256, ScheduleKind::Float};
        }
        _halide_internal_error << "KQuantVecDotGenerator: family not yet converted\n";
        return {};
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(KQuantVecDotGenerator, k_quant_vec_dot)
