// Generic, GeneratorParam-driven quantize/dequantize pair for GGML's
// K-quant super-block formats (see quant_components.h's make_k_quant_scheme/
// CombineBits/PlanarBitPack/K4ScaleMinPack/Q3KScalePack for the reusable
// Approximation pieces this assembles). "Q2_K"/"Q3_K"/"Q4_K"/"Q5_K"/"Q6_K" are not
// distinct C++ classes here -- they're just different GENERATOR_ARGS
// instantiations of the same generator template, registered in
// CMakeLists.txt as q4_k_quantize/q4_k_dequantize etc. -- following the
// exact same shape as lookup_table_quant_generators.cpp's
// LookupTableCodecGenerator<Direction> (see that file's header comment for
// the full rationale: one shared configure() builds the whole pipeline once
// from a real ImageParam, each direction just adopts whichever half applies
// via add_input(const ImageParam&)/add_output(const Func&); generate() is
// an empty stub).
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
// GeneratorParam combination, since each K-quant format's field layout,
// scale scheme, and code bit-width all differ.
enum class Family { Q2_K,
                    Q3_K,
                    Q4_K,
                    Q5_K,
                    Q6_K };

template<Direction dir>
class KQuantCodecGenerator : public CodecGeneratorBase<KQuantCodecGenerator<dir>, dir> {
public:
    GeneratorParam<Family> family{
        "family",
        Family::Q4_K,
        {{"q2_k", Family::Q2_K},
         {"q3_k", Family::Q3_K},
         {"q4_k", Family::Q4_K},
         {"q5_k", Family::Q5_K},
         {"q6_k", Family::Q6_K}}};

    SchemeAndBytes build_scheme() const {
        // switch's controlling expression can't resolve GeneratorParam<T>'s
        // implicit conversion operators unambiguously -- .value() sidesteps
        // that by returning the plain Family directly. Each make_*_scheme()
        // now returns its own block_bytes alongside the scheme (computed from
        // the same field list it builds internally), so there's no byte
        // arithmetic to duplicate here.
        switch (family.value()) {
        case Family::Q2_K:
            return make_q2_k_scheme();  // {scales[16]; qs[64]; fp16 d; fp16 dmin;}
        case Family::Q3_K:
            return make_q3_k_scheme();  // {hmask[32]; qs[64]; scales[12]; fp16 d;}
        case Family::Q4_K:
            return make_q4_k_scheme();  // {fp16 d; fp16 dmin; scales[12]; qs[128];}
        case Family::Q5_K:
            return make_q5_k_scheme();  // {fp16 d; fp16 dmin; scales[12]; qh[32]; qs[128];}
        case Family::Q6_K:
            return make_q6_k_scheme();  // {ql[128]; qh[64]; scales[16]; fp16 d;}
        }
        _halide_internal_error << "unreachable Family\n";
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(KQuantCodecGenerator<Direction::Quantize>, k_quant_quantize)
HALIDE_REGISTER_GENERATOR(KQuantCodecGenerator<Direction::Dequantize>, k_quant_dequantize)
