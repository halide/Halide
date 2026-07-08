// Generic, GeneratorParam-driven quantize/dequantize pair for GGML's
// codebook-quantized (lookup-table) formats (see quant_components.h's
// LookupTableQuantize/E8M0Pack for the reusable Approximation pieces this
// assembles). "IQ4_NL"/"MXFP4" are not distinct C++ classes here -- they're
// just different GENERATOR_ARGS instantiations of the same generator
// template, registered in CMakeLists.txt as iq4_nl_quantize/mxfp4_quantize
// etc. -- following the same shape as symmetric_quant_generators.cpp's
// SymmetricCodecGenerator<Direction> (see that file's header comment for the
// full rationale: one shared configure() builds the whole pipeline once from
// a real ImageParam, each direction just adopts whichever half applies via
// add_input(const ImageParam&)/add_output(const Func&); generate() is an
// empty stub).
//
// generate() never calls Approximation::encode()/decode() directly -- only
// through Func::approximate_by() and Pipeline::compute_offline(). This
// configure()/generate() body is identical across every *_quant_generators.cpp
// file in this directory, so it lives in codec_generator_base.h's
// CodecGeneratorBase<Derived, dir> instead of being repeated here -- this
// class only needs to supply its own GeneratorParams and a build_scheme().

#include "Halide.h"

#include "codec_generator_base.h"
#include "quant_components.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

// Which of quant_components.h's make_*_scheme() factories to use, and the
// on-disk block size that goes with it -- not derivable from a plain
// GeneratorParam combination the way the affine/symmetric family's
// SchemeKind's block_bytes is, since each codebook has its own fixed layout.
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

template<Direction dir>
class LookupTableCodecGenerator : public CodecGeneratorBase<LookupTableCodecGenerator<dir>, dir> {
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

    SchemeAndBytes build_scheme() const {
        // switch's controlling expression can't resolve GeneratorParam<T>'s
        // implicit conversion operators unambiguously -- .value() sidesteps
        // that by returning the plain Family directly.
        switch (family.value()) {
        case Family::IQ4_NL:
            return {make_iq4_nl_scheme(), 18};  // {fp16 d; qs[16];}
        case Family::MXFP4:
            return {make_mxfp4_scheme(), 17};  // {e8m0 e; qs[16];}
        case Family::TQ2_0:
            return {make_tq2_0_scheme(), 66};  // {qs[64]; fp16 d;}
        case Family::TQ1_0:
            return {make_tq1_0_scheme(), 54};  // {qs[48]; qh[4]; fp16 d;}
        case Family::NVFP4:
            return {make_nvfp4_scheme(), 36};  // {d[4]; qs[32];}
        case Family::IQ2_S:
            return {make_iq2_s_scheme(), 82};  // {fp16 d; qs[32]; signs[32]; qh[8]; scales[8];}
        case Family::IQ3_XXS:
            return {make_iq3_xxs_scheme(), 98};  // {fp16 d; qs[64]; scales_and_signs[32];}
        case Family::IQ3_S:
            return {make_iq3_s_scheme(), 110};  // {fp16 d; qs[64]; qh[8]; signs[32]; scales[4];}
        case Family::IQ4_XS:
            return {make_iq4_xs_scheme(), 136};  // {fp16 d; scales_h[2]; scales_l[4]; qs[128];}
        // Importance-matrix-only (dequantize direction only -- no quantize
        // library is built for these; SeveredEncode stands in for the missing
        // forward map).
        case Family::IQ2_XS:
            return {make_iq2_xs_scheme(), 74};
        case Family::IQ2_XXS:
            return {make_iq2_xxs_scheme(), 66};
        case Family::IQ1_S:
            return {make_iq1_s_scheme(), 50};
        case Family::IQ1_M:
            return {make_iq1_m_scheme(), 56};
        }
        _halide_internal_error << "unreachable Family\n";
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(LookupTableCodecGenerator<Direction::Quantize>, lookup_table_quantize)
HALIDE_REGISTER_GENERATOR(LookupTableCodecGenerator<Direction::Dequantize>, lookup_table_dequantize)
