// Generic, GeneratorParam-driven quantize/dequantize pair for GGML's legacy
// per-block quantized formats (see quant_components.h for the reusable
// Approximation pieces this assembles). "Q4_0"/"Q4_1"/"Q5_0"/"Q5_1"/"Q8_0"/
// "Q8_1" are not distinct C++ classes here -- they're just different
// GENERATOR_ARGS instantiations of the same generator template, registered
// in CMakeLists.txt as e.g. q4_0_quantize/q4_0_dequantize.
//
// Quantize and dequantize share every GeneratorParam (the scheme they
// build is identical, just run in opposite directions), so rather than two
// classes each redeclaring the same params, this is one class template
// parameterized on Direction, following
// apps/linear_algebra/src/blas_l1_generators.cpp's AXPYGenerator<T>
// precedent (one generator template, registered multiple times under
// different names/template args).
//
// The whole pipeline -- for *either* direction -- is built once in
// configure(), not generate(): a single Func::approximate_by() +
// Pipeline::compute_offline() call on a genuinely real ImageParam (not a
// placeholder) produces both an "offline" half (the encode/quantize side,
// still depending on that real ImageParam) and an "online" half (the
// decode/dequantize side, reading from whatever ImageParam
// compute_offline() severed it to instead). Each direction just adopts
// whichever half applies to it as its own Input/Output, via
// GeneratorBase::add_input(const ImageParam&)/add_output(const Func&) --
// new overloads added to Generator.h/.cpp for exactly this use (see there),
// since the stock add_input<Buffer<>>()/add_output<Buffer<>>() only ever
// mint fresh, undefined ports for generate() to fill in later. generate()
// is therefore an empty stub: by the time it would run, there's nothing
// left to do.
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

// Which of quant_components.h's make_*_scheme() factories to use -- the one
// axis that can't be reduced to a GeneratorParam value alone, since each
// scheme needs a different subset/arity of the other params below.
enum class SchemeKind { Symmetric,
                        Affine,
                        Symmetric5Bit,
                        Affine5Bit,
                        SymmetricByteSum,
                        Q8K };

template<Direction dir>
class SymmetricCodecGenerator : public CodecGeneratorBase<SymmetricCodecGenerator<dir>, dir> {
public:
    GeneratorParam<int> block_size{"block_size", 32};
    GeneratorParam<int> qmax{"qmax", 127};
    GeneratorParam<int> code_bits{"code_bits", 8};
    GeneratorParam<int> levels{"levels", 15};
    GeneratorParam<RoundingMode> rounding{
        "rounding",
        RoundingMode::Nearest,
        {{"nearest", RoundingMode::Nearest},
         {"truncate_half_up_with_offset", RoundingMode::TruncateHalfUpWithOffset},
         {"sign_only", RoundingMode::SignOnly}}};
    GeneratorParam<ScaleAnchor> anchor{
        "anchor",
        ScaleAnchor::AbsMax,
        {{"abs_max", ScaleAnchor::AbsMax},
         {"extreme_signed", ScaleAnchor::ExtremeSignedValue},
         {"mean_abs", ScaleAnchor::MeanAbs}}};
    GeneratorParam<AffineRounding> affine_rounding{
        "affine_rounding",
        AffineRounding::ClampedInt8,
        {{"clamped_int8", AffineRounding::ClampedInt8},
         {"unclamped_uint8", AffineRounding::UnclampedUint8}}};
    GeneratorParam<SchemeKind> kind{
        "kind",
        SchemeKind::Symmetric,
        {{"symmetric", SchemeKind::Symmetric},
         {"affine", SchemeKind::Affine},
         {"symmetric_5bit", SchemeKind::Symmetric5Bit},
         {"affine_5bit", SchemeKind::Affine5Bit},
         {"symmetric_byte_sum", SchemeKind::SymmetricByteSum},
         {"q8k", SchemeKind::Q8K}}};

    SchemeAndBytes build_scheme() const {
        // switch's controlling expression can't resolve GeneratorParam<T>'s
        // implicit conversion operators unambiguously -- .value() sidesteps
        // that by returning the plain SchemeKind directly.
        switch (kind.value()) {
        case SchemeKind::Symmetric: {
            int code_bytes = code_bits == 4 ? block_size / 2 : code_bits == 1 ? block_size / 8 :
                                                                                block_size;
            int bytes = 2 + code_bytes;
            return {make_symmetric_block_scheme(block_size, qmax, rounding, anchor, code_bits), bytes};
        }
        case SchemeKind::Affine: {
            int bytes = 4 + (code_bits == 4 ? block_size / 2 : block_size);
            return {make_affine_block_scheme(block_size, levels, affine_rounding, code_bits), bytes};
        }
        case SchemeKind::Symmetric5Bit: {
            int bytes = 2 + 4 + block_size / 2;
            return {make_symmetric_5bit_block_scheme(block_size, qmax), bytes};
        }
        case SchemeKind::Affine5Bit: {
            int bytes = 4 + 4 + block_size / 2;
            return {make_affine_5bit_block_scheme(block_size, levels, affine_rounding), bytes};
        }
        case SchemeKind::SymmetricByteSum: {
            int bytes = 4 + block_size;
            return {make_symmetric_byte_sum_block_scheme(block_size, qmax), bytes};
        }
        case SchemeKind::Q8K: {
            int bytes = 4 + block_size + (block_size / 16) * 2;
            return {make_q8_k_scheme(block_size, qmax), bytes};
        }
        }
        _halide_internal_error << "unreachable SchemeKind\n";
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SymmetricCodecGenerator<Direction::Quantize>, symmetric_quantize)
HALIDE_REGISTER_GENERATOR(SymmetricCodecGenerator<Direction::Dequantize>, symmetric_dequantize)
