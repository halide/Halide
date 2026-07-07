#pragma once

// Reusable Approximation components for GGML-style per-block quantized
// weight formats -- see doc/ApproximationDesign.md and the plan this file
// implements for the rationale. Every weight format is built by composing
// these kinds of pieces via Halide::Compose/Halide::Apply (and, for the
// extern-delegated formats, Halide::TrustedInverse) into a scheme (see the
// make_*_scheme()/make_*_codec() factory functions below), which the
// Generators then splice in via Func::approximate_by()/
// Pipeline::compute_offline() -- never by calling Approximation::encode()/
// decode() directly.
//
//   1. BlockReshape -- lossless relayout: flat values <-> (kk, blk).
//   2. SymmetricAffineQuantize/AffineQuantize -- the actual lossy step:
//      block values <-> (integer codes, one or two float(s) per block).
//   3a. Fp16Pack/PlanarBitPack/BytePack/FiveBitPack -- per-field packing:
//      a typed field (codes, scale, min) <-> its own on-disk byte encoding.
//   3b. AppendCodeSum -- a derived extra field, computed from other
//      already-encoded fields rather than from the original values.
//   3c. StructPack -- concatenates N already-packed fields into one
//      byte-addressed buffer, matching a specific on-disk block layout
//      (e.g. block_q4_0).
//   4. Extern-delegated formats (codebook/K-quant/IQ grid/IQ4_XS): their
//      quantize is an opaque GGML extern (ExternQuantize), paired with a
//      compositional dequantize via Halide::TrustedInverse. The extra
//      decode-only math leaves those need -- Codebook (codes -> table[codes]),
//      ScaleDequant/TwoLevelScaleDequant (the scale multiply), CombineBits
//      (K-quant split codes) -- live in section 4 alongside ExternQuantize.
//
// None of these know about any specific GGML type name -- "Q4_0"/"Q4_1"/etc.
// are just particular parameter choices, assembled where the Generators live
// (symmetric_quant_generators.cpp), not encoded here.
//
// None of these components call Func::bound() on their own intermediate
// Funcs, even where a range is intrinsically known (e.g. codes(kk, blk) for
// kk in [0, block_size)): with everything left at its default (inline)
// schedule, as it is here, Halide already infers the true required range by
// propagating backward from wherever the final packed buffer is actually
// realized or scheduled (a Generator's Output dim bounds, or an explicit
// realize() shape) -- an explicit bound() on an inlined Func is simply
// ignored ("meaningless... because the function is scheduled inline", per
// Halide's own warning). bound() only becomes useful once a component's Func
// is deliberately scheduled non-inline (compute_root/compute_at), which is a
// scheduling-time decision made where that happens, not decided here.

// Only the aggregated Halide.h is installed for apps to consume (individual
// per-class headers like Approximation.h are not) -- it already pulls in
// Approximation/Compose/Apply/Pipeline::compute_offline.
#include "Halide.h"

#include "iq_grids_data.h"

namespace ggml_halide {

// ---------------------------------------------------------------------------
// 1. Lossless relayout.
// ---------------------------------------------------------------------------

// A lossless flat <-> block reshape. In the common one-dimensional case
// (BlockReshape(block_size)), packed(kk, blk) = flat(blk*block_size + kk) --
// one within-block index kk in [0, block_size), one block index blk.
//
// The general case (BlockReshape({e0, e1, ...})) unflattens the within-block
// index into *several* dimensions, innermost/fastest-varying first, so a
// component whose values have nested block structure can index those
// dimensions directly instead of re-deriving them from a flat kk via div/mod.
// E.g. an IQ 256-element superblock structured as group(8) x l(4) x elem(8)
// uses extents {8, 4, 8}: packed(elem, l, group, blk), where the flat
// within-block index kk = elem + 8*l + 32*group (product of extents = block
// size). The single-int constructor is exactly the one-extent case.
class BlockReshape : public Halide::Approximation {
public:
    explicit BlockReshape(int block_size)
        : extents_{block_size} {
    }
    explicit BlockReshape(std::vector<int> extents)
        : extents_(std::move(extents)) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        std::vector<Var> dims = block_vars();
        Var blk("blk");

        // packed(d0, d1, ..., blk) = flat(blk*block_size + (d0 + e0*d1 + ...)).
        Expr within = cast<int>(0);
        int stride = 1;
        for (size_t i = 0; i < dims.size(); i++) {
            within += dims[i] * stride;
            stride *= extents_[i];
        }
        std::vector<Var> args = dims;
        args.push_back(blk);
        Func packed("block_reshape_packed");
        packed(args) = flat(blk * block_size() + within);
        return {{packed}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func packed = encoded[0];
        Var k("k");

        // flat(k) = packed(kk%e0, (kk/e0)%e1, ..., blk), kk = k % block_size.
        Expr rem = k % block_size();
        std::vector<Expr> args;
        for (int e : extents_) {
            args.push_back(rem % e);
            rem = rem / e;
        }
        args.push_back(k / block_size());
        Func flat("block_reshape_unpacked");
        flat(k) = packed(args);
        return {{flat}, {}};
    }

private:
    std::vector<int> extents_;

    int block_size() const {
        int p = 1;
        for (int e : extents_) {
            p *= e;
        }
        return p;
    }
    // One Var per within-block dimension; the familiar "kk" in the common
    // one-dimensional case, "d0"/"d1"/... otherwise.
    std::vector<Halide::Var> block_vars() const {
        std::vector<Halide::Var> vs;
        for (size_t i = 0; i < extents_.size(); i++) {
            vs.push_back(extents_.size() == 1 ? Halide::Var("kk") : Halide::Var("d" + std::to_string(i)));
        }
        return vs;
    }
};

// ---------------------------------------------------------------------------
// 2. The lossy step.
// ---------------------------------------------------------------------------

// GGML's own reference quantizers round differently depending on the target
// bit width, not out of taste but because they use different formulas:
//   - Nearest: plain round-half-away-from-zero (Q8_0's quantize_row_q8_0_ref
//     uses roundf()). Halide's round() matches this exactly.
//   - TruncateHalfUpWithOffset: a truncate-based "+qmax+0.5f then cast"
//     trick used by nibble-packed formats (Q4_0's quantize_row_q4_0_ref).
//     Verified by hand this is round-half-*up*, not round-half-away-from-
//     zero: floor(x+8.5) at x=-0.5 gives 8 (rounds toward +inf), whereas
//     round-half-away-from-zero would give 7.
//   - SignOnly: code = sign(x0) in {-1, +1}, ignoring magnitude entirely --
//     Q1_0's actual quantizer (1-bit codes, no rounding to speak of; paired
//     with ScaleAnchor::MeanAbs below, not qmax-based like every other
//     anchor).
//   - NearestEvenClampedHigh: round-half-to-even (not round-half-away-from-
//     zero like Nearest), then clamp only the high end to qmax -- Q8_K's
//     actual quantizer. GGML computes this via a magic-number float trick
//     (nearest_int(), reproduced by nearest_int() below) that exploits the
//     default IEEE-754 round-to-nearest-even rounding of the addition
//     itself; Halide has no round-to-even primitive exposed, so the same
//     bit trick is used here to match bit-for-bit. Always paired with
//     ScaleAnchor::ExtremeSignedValueTwoStep below.
enum class RoundingMode { Nearest,
                          TruncateHalfUpWithOffset,
                          SignOnly,
                          NearestEvenClampedHigh };

// Same magic-number trick as GGML's static inline nearest_int() in
// src/ggml-quants.c: adding 1.5*2^23 forces the CPU's default round-to-
// nearest-even addition to round fval's fractional part, then the rounded
// integer is recovered from the float's mantissa bits.
inline Halide::Expr nearest_int(Halide::Expr fval) {
    using namespace Halide;
    Expr val = fval + 12582912.0f;  // 1.5 * 2^23
    Expr bits = reinterpret<int32_t>(val);
    return (bits & 0x007fffff) - 0x00400000;
}

// How a block's scale is derived from its values -- this is a second,
// independent axis GGML varies per format, not just rounding:
//   - AbsMax: scale = max(|v|) / qmax -- ordinary symmetric quantization
//     (Q8_0).
//   - ExtremeSignedValue: scale = -extreme / qmax, where `extreme` is the
//     *signed* value with the largest magnitude in the block (ties keep the
//     first-seen value, matching GGML's single left-to-right loop with a
//     strict '<' comparison). This deliberately anchors the block's most
//     extreme value at code -qmax, using the full negative side of an
//     asymmetric signed range like [-8, 7] (Q4_0).
//   - MeanAbs: scale = mean(|v|) over the block (a sum reduction divided by
//     block_size, not a max reduction divided by qmax) -- Q1_0's anchor,
//     always paired with RoundingMode::SignOnly.
//   - ExtremeSignedValueTwoStep: mathematically the same value as
//     ExtremeSignedValue (scale = -extreme/qmax), but computed as GGML's
//     own two *separate* divisions -- `iscale = -qmax/extreme` first, then
//     `scale = 1/iscale` -- rather than the algebraically-equivalent single
//     multiply above. Floating point isn't associative, so these round
//     differently in the last bit; `iscale` itself (not a fresh 1/scale
//     recomputed afterward) is also what SymmetricAffineQuantize::encode()
//     uses to derive codes for this anchor, to stay bit-exact with GGML's
//     quantize_row_q8_K_ref -- see encode()'s `id`/`scale` computation.
//     Always paired with RoundingMode::NearestEvenClampedHigh.
enum class ScaleAnchor { AbsMax,
                         ExtremeSignedValue,
                         MeanAbs,
                         ExtremeSignedValueTwoStep };

// encode(): block(kk, blk) -> {codes(kk, blk) in [-qmax, qmax-1], scale(blk)}.
// decode(): {codes, scale} -> cast<float>(codes) * scale -- this half is
// exactly the same regardless of rounding/anchor (both Q4_0's and Q8_0's
// existing hand-written dequantize math already reduce to this one formula).
class SymmetricAffineQuantize : public Halide::Approximation {
public:
    SymmetricAffineQuantize(int block_size, int qmax, RoundingMode rounding, ScaleAnchor anchor)
        : block_size_(block_size), qmax_(qmax), rounding_(rounding), anchor_(anchor) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func block = inputs[0];  // block(kk, blk)
        Var kk("kk"), blk("blk");
        RDom r(0, block_size_, "r");

        Func stat("affine_quantize_stat");
        Func scale("affine_quantize_scale");
        Func id("affine_quantize_id");
        if (anchor_ == ScaleAnchor::AbsMax) {
            stat(blk) = 0.0f;
            stat(blk) = max(stat(blk), abs(block(r, blk)));
            scale(blk) = stat(blk) / (float)qmax_;
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == ScaleAnchor::ExtremeSignedValue) {
            stat(blk) = Tuple(0.0f, 0.0f);  // {amax, extreme_signed}
            Expr v = block(r, blk);
            Expr take = abs(v) > stat(blk)[0];
            stat(blk) = Tuple(select(take, abs(v), stat(blk)[0]),
                              select(take, v, stat(blk)[1]));
            scale(blk) = stat(blk)[1] * (-1.0f / (float)qmax_);
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == ScaleAnchor::MeanAbs) {
            stat(blk) = 0.0f;
            stat(blk) += abs(block(r, blk));
            scale(blk) = stat(blk) / (float)block_size_;
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else {                            // ExtremeSignedValueTwoStep
            stat(blk) = Tuple(0.0f, 0.0f);  // {amax, extreme_signed}
            Expr v = block(r, blk);
            Expr take = abs(v) > stat(blk)[0];
            stat(blk) = Tuple(select(take, abs(v), stat(blk)[0]),
                              select(take, v, stat(blk)[1]));
            // `id` (== GGML's `iscale`) is computed FIRST here, and `scale`
            // is derived from it -- the reverse order of every other
            // anchor above -- because GGML's own reference computes
            // `iscale = -qmax/extreme` then `d = 1/iscale` as two
            // *separate* divisions, and quantizes using `iscale` directly.
            // Re-deriving `id` as `1/scale` afterward (like every other
            // anchor does) would round through an extra reciprocal
            // (`1/(1/iscale)`) that isn't guaranteed to reproduce `iscale`
            // bit-for-bit.
            id(blk) = select(stat(blk)[0] == 0.0f, 0.0f, (-1.0f * (float)qmax_) / stat(blk)[1]);
            scale(blk) = select(id(blk) != 0.0f, 1.0f / id(blk), 0.0f);
        }
        // stat has an update definition, so it must be scheduled somewhere
        // (Halide can't inline it) -- like SymmetricRowQuantize's `amax` in
        // approximation_composition.cpp, that's left to the caller via
        // `handles`, not decided here.

        Expr x0 = block(kk, blk) * id(blk);

        Func codes("affine_quantize_codes");
        if (rounding_ == RoundingMode::Nearest) {
            // Matches Q8_0's actual (bit-exact-verified) reference: no
            // explicit clamp, since id was derived so |x0| doesn't exceed
            // qmax in practice.
            codes(kk, blk) = cast<int8_t>(round(x0));
        } else if (rounding_ == RoundingMode::TruncateHalfUpWithOffset) {
            Expr raw = cast<int32_t>(cast<int8_t>(x0 + (float)qmax_ + 0.5f));
            codes(kk, blk) = cast<int8_t>(min(raw, 2 * qmax_ - 1) - qmax_);
        } else if (rounding_ == RoundingMode::SignOnly) {
            codes(kk, blk) = cast<int8_t>(select(block(kk, blk) >= 0.0f, 1, -1));
        } else {  // NearestEvenClampedHigh
            Expr q_raw = nearest_int(x0);
            codes(kk, blk) = cast<int8_t>(min(qmax_, q_raw));
        }

        return {{codes, scale}, {stat}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func codes = encoded[0], scale = encoded[1];
        Var kk("kk"), blk("blk");
        Func dequantized("affine_dequantized");
        dequantized(kk, blk) = cast<float>(codes(kk, blk)) * scale(blk);
        return {{dequantized}, {}};
    }

private:
    int block_size_, qmax_;
    RoundingMode rounding_;
    ScaleAnchor anchor_;
};

// How AffineQuantize rounds+truncates code = round((x-min)*id) into its
// final representable range -- a different formula than SymmetricAffineQuantize's
// RoundingMode, and not a variation on it: there's no centering/offset here
// (codes are naturally unsigned starting at 0), and GGML's two affine legacy
// formats don't even agree on whether to clamp at all:
//   - ClampedInt8: (int8_t)(v+0.5f), then an explicit min(.., levels) --
//     Q4_1's exact formula.
//   - UnclampedUint8: (uint8_t)(v+0.5f) directly, no further clamp -- Q5_1's
//     exact formula. GGML's own reference genuinely doesn't clamp this one
//     (verified against quantize_row_q5_1_ref); reproduced faithfully since
//     quantize output is checked bit-exact.
enum class AffineRounding { ClampedInt8,
                            UnclampedUint8 };

// encode(): block(kk, blk) -> {codes(kk, blk) in [0, levels], scale(blk),
// min(blk)}. decode(): {codes, scale, min} -> cast<float>(codes)*scale + min.
// The min-max (not max-abs) scale derivation is what makes this "affine"
// rather than "symmetric" -- every value in a block is representable, not
// just those centered on zero, at the cost of needing a second per-block
// float (Q4_1/Q5_1's 'm').
class AffineQuantize : public Halide::Approximation {
public:
    AffineQuantize(int block_size, int levels, AffineRounding rounding)
        : block_size_(block_size), levels_(levels), rounding_(rounding) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func block = inputs[0];  // block(kk, blk)
        Var kk("kk"), blk("blk");
        RDom r(0, block_size_, "r");

        // Plain min/max reduction -- unlike SymmetricAffineQuantize's
        // ScaleAnchor::ExtremeSignedValue, min and max are independent here,
        // forming an affine (not centered-on-zero) range.
        Func stat("affine_quantize_minmax");
        stat(blk) = Tuple(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());
        Expr v = block(r, blk);
        stat(blk) = Tuple(min(stat(blk)[0], v), max(stat(blk)[1], v));

        Func scale("affine_quantize_scale");
        scale(blk) = (stat(blk)[1] - stat(blk)[0]) / (float)levels_;
        Func minv("affine_quantize_min");
        minv(blk) = stat(blk)[0];

        Expr id = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        Expr x0 = (block(kk, blk) - minv(blk)) * id;

        Func codes("affine_quantize_codes");
        if (rounding_ == AffineRounding::ClampedInt8) {
            Expr raw = cast<int32_t>(cast<int8_t>(x0 + 0.5f));
            codes(kk, blk) = cast<int8_t>(min(raw, levels_));
        } else {
            codes(kk, blk) = cast<int8_t>(cast<uint8_t>(x0 + 0.5f));
        }

        return {{codes, scale, minv}, {stat}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func codes = encoded[0], scale = encoded[1], minv = encoded[2];
        Var kk("kk"), blk("blk");
        Func dequantized("affine_dequantized_am");
        dequantized(kk, blk) = cast<float>(codes(kk, blk)) * scale(blk) + minv(blk);
        return {{dequantized}, {}};
    }

private:
    int block_size_, levels_;
    AffineRounding rounding_;
};

// ---------------------------------------------------------------------------
// 3a. Per-field bit packing.
// ---------------------------------------------------------------------------

// Assemble a 4-byte little-endian word starting at bytes(base, blk) into a
// uint32 -- the on-disk byte order shared by F32Pack's scale, FiveBitPack's
// qh, and IQ3_XXS's aux32.
inline Halide::Expr le_u32(Halide::Func bytes, Halide::Expr base, Halide::Var blk) {
    using namespace Halide;
    return cast<uint32_t>(bytes(base + 0, blk)) | (cast<uint32_t>(bytes(base + 1, blk)) << 8) |
           (cast<uint32_t>(bytes(base + 2, blk)) << 16) | (cast<uint32_t>(bytes(base + 3, blk)) << 24);
}

// encode(float scale) -> 2 bytes; decode(2 bytes) -> float. Matches every
// format's existing fp16 delta-byte code (byte0 = bits&0xff, byte1 =
// (bits>>8)&0xff).
class Fp16Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0];  // scale(blk)
        Var byte("byte"), blk("blk");
        Expr bits = reinterpret<uint16_t>(cast<float16_t>(scale(blk)));
        Func bytes("fp16_pack_bytes");
        bytes(byte, blk) = select(byte == 0, cast<uint8_t>(bits & 0xff), cast<uint8_t>((bits >> 8) & 0xff));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 2)
        Var blk("blk");
        Expr lo = cast<uint16_t>(bytes(0, blk));
        Expr hi = cast<uint16_t>(bytes(1, blk));
        Func scale("fp16_pack_scale");
        scale(blk) = cast<float>(reinterpret<float16_t>(lo | (hi << 8)));
        return {{scale}, {}};
    }
};

// encode(float scale) -> 4 bytes (plain IEEE-754 binary32, little-endian);
// decode(4 bytes) -> float -- Q8_K's scale format, the one format here whose
// delta is a full float, not fp16 (matching block_q8_K's `float d;`, not
// GGML's usual `ggml_fp16_t d;`).
class F32Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0];  // scale(blk)
        Var byte("byte"), blk("blk");
        Expr bits = reinterpret<uint32_t>(scale(blk));
        Func bytes("f32_pack_bytes");
        bytes(byte, blk) = cast<uint8_t>((bits >> (cast<uint32_t>(byte) * 8)) & 0xff);
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 4)
        Var blk("blk");
        Func scale("f32_pack_scale");
        scale(blk) = reinterpret<float>(le_u32(bytes, 0, blk));
        return {{scale}, {}};
    }
};

// encode(int16 values(g, blk)) -> 2 little-endian bytes each, byte `2g`/
// `2g+1` -- e.g. Q8_K's bsums[16] int16 array (one value per 16-element
// group, not one per block).
class Int16Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func values = inputs[0];  // values(g, blk)
        Var byte_idx("byte_idx"), blk("blk");
        Expr g = byte_idx / 2;
        Expr is_lo = (byte_idx % 2) == 0;
        Expr bits = reinterpret<uint16_t>(values(g, blk));
        Func bytes("int16_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(select(is_lo, bits & 0xff, (bits >> 8) & 0xff));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, 2*num_groups)
        Var g("g"), blk("blk");
        Expr lo = cast<uint16_t>(bytes(2 * g, blk));
        Expr hi = cast<uint16_t>(bytes(2 * g + 1, blk));
        Func values("int16_pack_values");
        values(g, blk) = reinterpret<int16_t>(cast<uint16_t>(lo | (hi << 8)));
        return {{values}, {}};
    }
};

// encode(float scale) -> 1 byte (an E8M0 power-of-two exponent, GGML's
// MXFP4/NVFP4 scale format); decode(1 byte) -> float. decode() reproduces
// ggml_e8m0_to_fp32_half's exact bit construction: d = 2^(e-128) for every
// e in [0, 255], computed via a subnormal-exploiting shift trick for e<2
// instead of a normal exponent-field write (both branches compute the same
// uniform 2^(e-128); see the comment inline). encode() is log2-based and
// therefore *not* bit-exact -- matching every other format's rationale for
// deferring quantize to GGML's own reference (see ExternQuantize
// below); it exists for interface completeness only, and is never actually
// exercised by MXFP4/NVFP4 today, since their real encode() is an extern
// call, not a round trip through this class.
class E8M0Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0];  // scale(blk)
        Var blk("blk");
        Expr log2e = 1.4426950408889634f;  // 1/log(2), since Halide only provides fast_log (natural log)
        Expr e = cast<int32_t>(round(fast_log(max(scale(blk), 1e-38f)) * log2e + 128.0f));
        Func byte("e8m0_pack_byte");
        Var byte_idx("byte_idx");
        byte(byte_idx, blk) = cast<uint8_t>(clamp(e, 0, 255));
        return {{byte}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func byte = encoded[0];  // byte(byte_idx, blk), byte_idx in [0, 1)
        Var blk("blk");
        Expr e = cast<uint32_t>(byte(0, blk));
        Expr bits = select(e < 2, cast<uint32_t>(0x00200000) << e, (e - 1) << 23);
        Func scale("e8m0_pack_scale");
        scale(blk) = reinterpret<float>(bits);
        return {{scale}, {}};
    }
};

// encode(float scale(sub, blk)) -> 1 byte per sub-block (a UE4M3 unsigned
// 4-exponent/3-mantissa float, GGML's NVFP4 per-sub-block scale format);
// decode(1 byte per sub-block) -> float(sub, blk). decode() reproduces
// ggml_ue4m3_to_fp32_half's exact construction: subnormal (exp==0) is
// man/512, normal is (1+man/8)*2^(exp-7), both halved; byte 0x00 or 0x7f
// (GGML's NVFP4 zero/sentinel bytes) decode to 0. Unlike Fp16Pack/E8M0Pack
// (exactly one scale value per block), this is meant to be used with
// ScaleDequant's `num_scales` > 1: the input/output Funcs here are
// already indexed by `sub` directly (one byte each, no further byte-within-
// field splitting needed, unlike Fp16Pack's 2-byte value). encode() is
// log2-based and therefore *not* bit-exact -- matching every other format's
// rationale for deferring quantize to GGML's own reference (see
// ExternQuantize above); it exists for interface completeness only,
// and is never actually exercised by NVFP4 today, since its real encode()
// is an extern call, not a round trip through this class.
class UE4M3Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0];  // scale(sub, blk)
        Var sub("sub"), blk("blk");
        Expr log2e = 1.4426950408889634f;  // 1/log(2), since Halide only provides fast_log (natural log)
        Expr v = scale(sub, blk) * 2.0f;
        Expr e = cast<int32_t>(floor(fast_log(max(v, 1e-38f)) * log2e)) + 7;
        Expr e_clamped = clamp(e, 0, 15);
        Expr scale_pow = pow(2.0f, cast<float>(e_clamped - 7));
        Expr man = cast<int32_t>(round((v / scale_pow - 1.0f) * 8.0f));
        Expr man_clamped = clamp(man, 0, 7);
        Func byte("ue4m3_pack_byte");
        byte(sub, blk) = cast<uint8_t>((e_clamped << 3) | man_clamped);
        return {{byte}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func byte = encoded[0];  // byte(sub, blk)
        Var sub("sub"), blk("blk");
        Expr ue = byte(sub, blk);                                  // codespell:ignore ue
        Expr is_zero = (ue == 0) || (ue == 0x7f);                  // codespell:ignore ue
        Expr exp_ = cast<int32_t>(cast<uint32_t>(ue) >> 3) & 0xf;  // codespell:ignore ue
        Expr man_ = cast<int32_t>(ue) & 0x7;                       // codespell:ignore ue
        Expr raw = select(exp_ == 0,
                          cast<float>(man_) / 512.0f,
                          (1.0f + cast<float>(man_) / 8.0f) * pow(2.0f, cast<float>(exp_ - 7)));
        Func scale("ue4m3_pack_scale");
        scale(sub, blk) = select(is_zero, 0.0f, raw * 0.5f);
        return {{scale}, {}};
    }
};

// The shared byte<->field decomposition behind every uniform-width,
// non-base-3 bit-packed field in this file (nibble packs, 2-bit packs, and
// the K-quants' rotating out-of-band high-bit arrays): a flat per-block
// index kk factors as
//   kk = outer*(plane_count*pos_count) + plane*pos_count + pos
// and each byte packs `plane_count` fixed-width `field_bits`-bit fields
// ("planes") -- byte `outer*pos_count + pos` holds element (outer, plane, pos)
// at bit-shift `plane*field_bits`. This one decomposition is exactly (verified
// by hand, not just formally) GGML's addressing for all of the following, each
// just a different (field_bits, plane_count, pos_count) instantiated directly
// at its call site below rather than through a named subclass, since the
// class itself is the entire "component" here -- a named wrapper would only
// rename a handful of integers, not remove any duplication:
//   - Nibble-packed formats (Q4_0, Q4_1, Q5_0's low bits, Q4_K, NVFP4, ...):
//     4-bit fields, 2 planes (low/high nibble), pos_count = window_size/2,
//     outer = which independent sub-block "window" (1 for a plain whole-block
//     low-half/high-half split, e.g. Q4_0/IQ4_NL; >1 for NVFP4/Q4_K/Q5_K's
//     independent windows) -- GGML's actual convention for every nibble-
//     packed format, not just the more obvious "adjacent pair per byte"
//     layout.
//   - TQ2_0/Q2_K/Q3_K/Q6_K's 2-bit fields: 2-bit fields, 4 planes, pos_count
//     = block_size/8 (= window_bytes), outer = which half of the block --
//     verified by hand against each format's original dequantize math (e.g.
//     TQ2_0's `half = gi/half_block; l = local/window_bytes; m =
//     local%window_bytes; byte_idx = half*window_bytes + m; shift = l*2`);
//     NOT a plain "4 consecutive elements per byte" packing.
//   - Q3_K's hmask / Q5_K's qh out-of-band high-bit arrays: 1-bit fields,
//     num_windows planes, pos_count = window_size, outer always 0 (the whole
//     array is one window_size*num_windows group) -- the "rotating bit
//     position" scheme GGML uses for an extra high bit per element (their own
//     source expresses this via a `half`/`iter`-based case split instead, but
//     it's the same addressing). Always paired with a lower-bit code via
//     CombineBits, never used standalone.
// `qmax` recenters already-decoded codes before splitting (0 leaves the raw
// field, e.g. a lookup-table index or a single out-of-band bit -- always 0
// for the two cases above). encode() OR-accumulates the planes into each
// byte via an RDom, exactly like BitPack/FiveBitPack's qh accumulation.
class PlanarBitPack : public Halide::Approximation {
public:
    PlanarBitPack(int field_bits, int plane_count, int pos_count, int qmax)
        : field_bits_(field_bits), plane_count_(plane_count), pos_count_(pos_count), qmax_(qmax) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var byte_idx("byte_idx"), blk("blk");
        int group = plane_count_ * pos_count_;
        Expr outer = byte_idx / pos_count_;
        Expr pos = byte_idx % pos_count_;

        RDom rp(0, plane_count_, "rp");
        Expr kk = outer * group + rp * pos_count_ + pos;
        Expr field = cast<uint32_t>(cast<int32_t>(codes(kk, blk)) + qmax_) & ((1u << field_bits_) - 1);
        Func bytes("planar_bit_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(0);
        bytes(byte_idx, blk) = bytes(byte_idx, blk) | cast<uint8_t>(field << (rp * field_bits_));
        return {{bytes}, {bytes}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var kk("kk"), blk("blk");
        int group = plane_count_ * pos_count_;
        Expr outer = kk / group;
        Expr rem = kk % group;
        Expr plane = rem / pos_count_;
        Expr pos = rem % pos_count_;
        Expr byte_idx = outer * pos_count_ + pos;
        Expr field = (cast<uint32_t>(bytes(byte_idx, blk)) >> (plane * field_bits_)) & ((1u << field_bits_) - 1);
        Func codes("planar_bit_pack_codes");
        codes(kk, blk) = cast<int8_t>(cast<int32_t>(field) - qmax_);
        return {{codes}, {}};
    }

private:
    int field_bits_, plane_count_, pos_count_, qmax_;
};

// encode(codes(kk, blk) signed in [-qmax, qmax-1]) -> {nibble_bytes(b, blk)
// block_size/2 bytes, qh_bytes(qb, blk) 4 bytes}; decode reverses it. Splits
// each 5-bit value into a 4-bit low nibble (packed like PlanarBitPack's
// nibble case above) and a 5th (0x10) high bit, OR-accumulated one bit per
// element into a 32-bit little-endian word -- GGML's Q5_0/Q5_1 layout.
// Verified by hand that qh's bit `kk` is element kk's own high bit (GGML's
// own code computes this via a low/high-half split for scalar-loop
// efficiency -- e.g. `(qh >> (byte_idx+12)) & 0x10` for the high half -- but
// that's an equivalent, more roundabout way of writing the same "bit kk of
// qh is element kk's high bit" fact used directly here). Like PlanarBitPack,
// `qmax` re-centers already-decoded codes before splitting -- 16 for Q5_0's
// symmetric codes, 0 for Q5_1's already-unsigned affine codes.
class FiveBitPack : public Halide::Approximation {
public:
    FiveBitPack(int block_size, int qmax)
        : block_size_(block_size), qmax_(qmax) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var b("b"), qb("qb"), kk("kk"), blk("blk");
        int half = block_size_ / 2;

        auto unsigned_at = [&](Expr k) -> Expr {
            return cast<uint32_t>(cast<int32_t>(codes(k, blk)) + qmax_);
        };

        Expr lo = cast<uint8_t>(unsigned_at(b) & 0xf);
        Expr hi = cast<uint8_t>(unsigned_at(b + half) & 0xf);
        Func nibble_bytes("five_bit_pack_nibbles");
        nibble_bytes(b, blk) = cast<uint8_t>(lo | (hi << 4));

        Func high_bit("five_bit_pack_high_bit");
        high_bit(kk, blk) = (unsigned_at(kk) >> 4) & 1u;

        RDom rk(0, block_size_, "rk");
        Func qh_accum("five_bit_pack_qh_accum");
        qh_accum(blk) = cast<uint32_t>(0);
        qh_accum(blk) = qh_accum(blk) | (high_bit(rk, blk) << rk);

        Func qh_bytes("five_bit_pack_qh_bytes");
        qh_bytes(qb, blk) = cast<uint8_t>((qh_accum(blk) >> (qb * 8)) & 0xff);

        return {{nibble_bytes, qh_bytes}, {qh_accum}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func nibble_bytes = encoded[0], qh_bytes = encoded[1];
        Var kk("kk"), blk("blk");
        int half = block_size_ / 2;
        Expr b = kk % half;
        Expr is_low = kk < half;
        Expr nbyte = nibble_bytes(b, blk);
        Expr nibble = cast<uint32_t>(select(is_low, nbyte & 0xf, (nbyte >> 4) & 0xf));

        Expr qh = le_u32(qh_bytes, 0, blk);
        Expr high_bit = (qh >> kk) & 1u;
        Expr unsigned_code = nibble | (high_bit << 4);

        Func codes("five_bit_pack_codes");
        codes(kk, blk) = cast<int8_t>(cast<int32_t>(unsigned_code) - qmax_);
        return {{codes}, {}};
    }

private:
    int block_size_, qmax_;
};

// encode(codes(kk, blk) signed in {-1, +1}) -> block_size/8 bytes; decode
// reverses it. Packs one sign bit per element, 8 elements per byte, bit
// `kk % 8` of byte `kk / 8` set when code is +1 -- Q1_0's layout (paired
// with RoundingMode::SignOnly/ScaleAnchor::MeanAbs above). Accumulates via
// an OR-reduction exactly like FiveBitPack's qh_accum, just one full byte's
// worth of bits at a time instead of a cross-block 32-bit word.
class BitPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var byte_idx("byte_idx"), blk("blk");

        Func bit("bit_pack_bit");
        Var kk("kk");
        bit(kk, blk) = cast<uint8_t>(select(codes(kk, blk) > 0, 1, 0));

        RDom rb(0, 8, "rb");
        Func bytes("bit_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(0);
        bytes(byte_idx, blk) = bytes(byte_idx, blk) | cast<uint8_t>(bit(byte_idx * 8 + rb, blk) << rb);

        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, block_size/8)
        Var kk("kk"), blk("blk");
        Expr byte_idx = kk / 8;
        Expr bit_off = kk % 8;
        Expr bit = (cast<uint32_t>(bytes(byte_idx, blk)) >> bit_off) & 1u;
        Func codes("bit_pack_codes");
        codes(kk, blk) = cast<int8_t>(select(bit != 0, 1, -1));
        return {{codes}, {}};
    }
};

// encode(codes(kk, blk) signed int8) -> 1 code per byte, same shape (the
// identity-shaped case PlanarBitPack's nibble/2-bit packing doesn't cover,
// used by e.g. Q8_0). Unlike PlanarBitPack, this formula has no precondition
// on kk's range (reinterpret is valid for any kk) -- it doesn't know or care
// what block_size is; bounds propagate backward from whatever actually
// consumes it.
class BytePack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var kk("kk"), blk("blk");
        Func bytes("byte_pack_bytes");
        bytes(kk, blk) = reinterpret<uint8_t>(codes(kk, blk));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var kk("kk"), blk("blk");
        Func codes("byte_pack_codes");
        codes(kk, blk) = reinterpret<int8_t>(bytes(kk, blk));
        return {{codes}, {}};
    }
};

// encode(codes(kk, blk) unsigned in {0, 1, 2}) -> 52 bytes {qs[48]; qh[4]};
// decode reverses it. Reproduces GGML's TQ1_0 base-3 digit packing exactly:
// 256 elements split into 3 sections -- a 32-byte/160-element run and a
// 16-byte/80-element run, each packing 5 trits/byte via
// `byte = ceil(digit_number * 256 / 243)` where `digit_number` is the
// 5-digit base-3 number with digit 0 (from codes(n*window+m)) most
// significant; then a 4-byte/16-element run packing only 4 real trits/byte
// (the 5th, least-significant "digit" is always 0, appended by an extra
// `*3` before the same ceiling-division formula) -- verified by hand against
// GGML's quantize_row_tq1_0_ref. decode() reverses the ceiling-division
// packing via the same multiply-truncate-rescale trick tq1_0_generators.cpp
// originally hand-rolled (extracting digit `n`'s value needs multiplier
// `3^n`, `n` counted from the most-significant digit -- not to be confused
// with a digit's place-value weight, which runs the other way). `qmax` has
// no effect here (there's no natural "centering" for a 3-valued digit, and
// this is never composed as a symmetric-scheme codec) -- codes are always
// the raw digit in [0, 3), suitable directly as a codebook index, matching
// TQ2_0's {-1, 0, 1, unused} table convention.
class TritPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var byte_idx("byte_idx"), blk("blk");

        auto digit = [&](Expr kk_expr) -> Expr {
            return cast<int32_t>(codes(kk_expr, blk));
        };
        auto ceil_pack = [&](Expr q) -> Expr {
            return cast<uint8_t>((cast<uint32_t>(q) * 256 + 242) / 243);
        };

        Expr m_a = clamp(byte_idx, 0, 31);
        Expr qa = digit(0 * 32 + m_a);
        qa = qa * 3 + digit(1 * 32 + m_a);
        qa = qa * 3 + digit(2 * 32 + m_a);
        qa = qa * 3 + digit(3 * 32 + m_a);
        qa = qa * 3 + digit(4 * 32 + m_a);

        Expr m_b = clamp(byte_idx - 32, 0, 15);
        Expr qb = digit(160 + 0 * 16 + m_b);
        qb = qb * 3 + digit(160 + 1 * 16 + m_b);
        qb = qb * 3 + digit(160 + 2 * 16 + m_b);
        qb = qb * 3 + digit(160 + 3 * 16 + m_b);
        qb = qb * 3 + digit(160 + 4 * 16 + m_b);

        Expr j_c = clamp(byte_idx - 48, 0, 3);
        Expr qc = digit(240 + j_c + 0 * 4);
        qc = qc * 3 + digit(240 + j_c + 1 * 4);
        qc = qc * 3 + digit(240 + j_c + 2 * 4);
        qc = qc * 3 + digit(240 + j_c + 3 * 4);
        qc = qc * 3;  // append the virtual, always-0 5th digit

        Func bytes("trit_pack_bytes");
        bytes(byte_idx, blk) = select(byte_idx < 32, ceil_pack(qa),
                                      byte_idx < 48, ceil_pack(qb),
                                      ceil_pack(qc));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, 52)
        Var kk("kk"), blk("blk");

        Expr n_a = kk / 32;
        Expr byte_a = kk % 32;

        Expr local_b = kk - 160;
        Expr n_b = local_b / 16;
        Expr byte_b = 32 + local_b % 16;

        Expr local_c = kk - 240;
        Expr n_c = local_c / 4;
        Expr byte_c = 48 + local_c % 4;

        Expr n = select(kk < 160, n_a, select(kk < 240, n_b, n_c));
        Expr byte_abs = select(kk < 160, byte_a, select(kk < 240, byte_b, byte_c));

        Expr byte_val = bytes(byte_abs, blk);
        Expr p3 = select(n == 0, 1, n == 1, 3, n == 2, 9, n == 3, 27, n == 4, 81, 243);

        Expr q_trunc = cast<uint8_t>(cast<uint32_t>(byte_val) * cast<uint32_t>(p3));
        Expr xi = cast<int32_t>((cast<uint16_t>(q_trunc) * 3) >> 8);

        Func codes("trit_pack_codes");
        codes(kk, blk) = cast<int8_t>(xi);
        return {{codes}, {}};
    }
};

// ---------------------------------------------------------------------------
// 3b. Derived extra fields (computed from other already-encoded fields, not
//     from the original values -- appended before struct-packing).
// ---------------------------------------------------------------------------

// encode({codes(kk, blk), scale(blk)}) -> {codes, scale, sum(blk)}, where
// sum(blk) = scale(blk) * sum_kk(cast<float>(codes(kk, blk))) -- GGML's
// Q8_1 "s" field, letting a paired vec_dot recover sum(dequantized values)
// cheaply from the block's own header instead of re-reducing codes itself.
// decode() discards sum and passes codes/scale through unchanged: it's a
// redundant, derivable quantity, not needed to reconstruct dequantized
// values, so there's nothing to invert. Arity-changing like FiveBitPack, but
// in the *encode* direction instead (2 inputs -> 3 outputs; decode then
// undoes it in the same direction rather than the mirror one, since sum
// isn't invertible into anything -- it's simply dropped).
class AppendCodeSum : public Halide::Approximation {
public:
    explicit AppendCodeSum(int block_size)
        : block_size_(block_size) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0], scale = inputs[1];
        Var blk("blk");
        RDom r(0, block_size_, "r");

        Func sum_i("append_code_sum_i");
        sum_i(blk) = 0;
        sum_i(blk) += cast<int32_t>(codes(r, blk));

        Func sum_f("append_code_sum");
        sum_f(blk) = cast<float>(sum_i(blk)) * scale(blk);

        return {{codes, scale, sum_f}, {sum_i}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        // sum (encoded[2]) is a redundant derived quantity -- pass
        // codes/scale through unchanged.
        return {{encoded[0], encoded[1]}, {}};
    }

private:
    int block_size_;
};

// encode({codes(kk, blk), scale(blk)}) -> {codes, scale, bsums(g, blk)},
// where bsums(g, blk) = sum_kk(codes(g*group_size+kk, blk)) as a plain
// int32-then-int16 integer sum -- GGML's Q8_K "bsums" field, letting a
// paired K-quant vec_dot recover each 16-element group's sum of raw int8
// codes cheaply, without rescaling by `scale` first (unlike Q8_1's
// AppendCodeSum above, whose "s" field is a single whole-block sum already
// multiplied by scale into a float). decode() discards bsums and passes
// codes/scale through unchanged, for the same reason AppendCodeSum does.
class AppendGroupSumsInt16 : public Halide::Approximation {
public:
    explicit AppendGroupSumsInt16(int group_size)
        : group_size_(group_size) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0], scale = inputs[1];
        Var g("g"), blk("blk");
        RDom rg(0, group_size_, "rg");

        Func sum_i("append_group_sums_i");
        sum_i(g, blk) = cast<int32_t>(0);
        sum_i(g, blk) += cast<int32_t>(codes(g * group_size_ + rg, blk));

        Func bsums("append_group_sums");
        bsums(g, blk) = cast<int16_t>(sum_i(g, blk));

        return {{codes, scale, bsums}, {sum_i}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        // bsums (encoded[2]) is a redundant derived quantity -- pass
        // codes/scale through unchanged.
        return {{encoded[0], encoded[1]}, {}};
    }

private:
    int group_size_;
};

// ---------------------------------------------------------------------------
// 3c. Concatenation into one byte buffer.
// ---------------------------------------------------------------------------

// Concatenates N already-packed, fixed-width byte fields into one
// byte-addressed buffer per block, at fixed offsets -- generalizes the
// per-format "select(byte==0, delta_byte0, byte==1, delta_byte1,
// packed(...))" pattern duplicated in every *_generators.cpp quantize
// function today.
//
// `field_widths[k]`/`input_index[k]` describe the k-th field *in output
// byte order*: it is `field_widths[k]` bytes wide, and its unpacked form is
// `inputs[input_index[k]]` (encode) / `encoded[input_index[k]]` (decode).
// This indirection exists because the vector StructPack receives is in
// whatever order the rest of the Compose/Apply chain produced it in (e.g.
// {codes_bytes, scale_bytes}), which need not match the on-disk field order
// (e.g. block_q4_0 stores the scale before the codes).
class StructPack : public Halide::Approximation {
public:
    StructPack(std::vector<int> field_widths, std::vector<int> input_index)
        : field_widths_(std::move(field_widths)), input_index_(std::move(input_index)) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Var byte("byte"), blk("blk");
        std::vector<int> offsets = offsets_in_output_order();

        // select()'s branches are all evaluated unconditionally (not
        // short-circuiting), so each field's local index is clamped to its
        // own valid range before use -- same idiom as q4_0_generators.cpp.
        Expr result = cast<uint8_t>(0);
        for (int k = (int)field_widths_.size() - 1; k >= 0; k--) {
            Expr local = clamp(byte - offsets[k], 0, field_widths_[k] - 1);
            Expr in_range = byte >= offsets[k] && byte < offsets[k] + field_widths_[k];
            result = select(in_range, inputs[input_index_[k]](local, blk), result);
        }

        Func packed("struct_pack_packed");
        packed(byte, blk) = result;
        return {{packed}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func packed = encoded[0];
        std::vector<int> offsets = offsets_in_output_order();
        Var local("local"), blk("blk");

        std::vector<Func> fields(field_widths_.size());
        for (int k = 0; k < (int)field_widths_.size(); k++) {
            Func field("struct_pack_field_" + std::to_string(k));
            field(local, blk) = packed(local + offsets[k], blk);
            fields[input_index_[k]] = field;
        }
        return {fields, {}};
    }

private:
    std::vector<int> offsets_in_output_order() const {
        std::vector<int> offsets(field_widths_.size());
        int acc = 0;
        for (size_t k = 0; k < field_widths_.size(); k++) {
            offsets[k] = acc;
            acc += field_widths_[k];
        }
        return offsets;
    }

    std::vector<int> field_widths_;
    std::vector<int> input_index_;
};

// ---------------------------------------------------------------------------
// Shared helpers for the extern-delegated formats.
// ---------------------------------------------------------------------------

// The extern quantize body, shared by every format whose real quantizer is a
// named GGML extern (see ggml_extern_quantize.cpp): it computes nothing
// itself, just names the *_quantize_via_ggml symbol and returns the whole
// packed byte buffer as a single 2-D uint8 Func. Wrapped as ExternQuantize
// below and used as the *encoder* half of a Halide::TrustedInverse, whose
// decoder half is the Compose that unpacks and dequantizes those bytes.
inline Halide::EncodeResult extern_quantize_blocks(std::vector<Halide::Func> inputs,
                                                   const std::string &extern_name) {
    using namespace Halide;
    Func flat = inputs[0];
    Func blocks(extern_name + "_blocks");
    std::vector<ExternFuncArgument> args = {flat};
    blocks.define_extern(extern_name, args, UInt(8), 2, NameMangling::C);
    return {{blocks}, {}};
}

// Decode the 2-byte little-endian fp16 delta stored at bytes(offset)/
// bytes(offset+1). Returns the delta as an Expr in `blk`. Same bit twiddling
// as Fp16Pack::decode, written inline here because the grid leaves below read
// their delta out of a raw byte buffer at a fixed offset rather than through a
// composed Fp16Pack stage.
inline Halide::Expr fp16_delta(Halide::Func bytes, int offset, Halide::Var blk) {
    using namespace Halide;
    Expr lo = cast<uint16_t>(bytes(offset + 0, blk));
    Expr hi = cast<uint16_t>(bytes(offset + 1, blk));
    return cast<float>(reinterpret<float16_t>(cast<uint16_t>(lo | (hi << 8))));
}

// Copy one of iq_grids_data.h's static constant codebook tables into a named
// Halide::Buffer the grid classes below index into.
template<typename T>
inline Halide::Buffer<T> make_grid_buffer(const T *data, int n, const char *name) {
    Halide::Buffer<T> buf(n, name);
    for (int i = 0; i < n; i++) {
        buf(i) = data[i];
    }
    return buf;
}

// ---------------------------------------------------------------------------
// 4. Extern-delegated quantize + decode-only dequantize-math leaves.
// ---------------------------------------------------------------------------
//
// The formats below (codebook, K-quant, IQ grid, IQ4_XS) all share one shape:
// their forward map (quantize) is an opaque offline black box -- a per-block
// nearest-codeword search, an iterative error-minimizing scale fit, a
// transcendental scale derivation -- that no composition of Halide Funcs
// reproduces bit-for-bit, so it is delegated to a named GGML extern (see
// ggml_extern_quantize.cpp). Their reverse map (dequantize) IS an ordinary,
// bit-exact composition of invertible primitives. Halide::TrustedInverse
// pairs the two: ExternQuantize (encode()) as the encoder half, a plain
// Compose (decode()) as the decoder half. The leaves here are the pieces of
// that decoder that aren't already covered by the packing/reshape components
// in sections 1-3 -- the codebook lookup and the scale-multiply math. Each is
// decode-only: its encode() is exactly the opaque forward map deferred to the
// extern, so it is never called (it only ever lives inside a TrustedInverse's
// decoder) and traps if it somehow is.

// The encoder half of every extern-delegated format's TrustedInverse: encode()
// delegates to the named GGML extern (extern_quantize_blocks), producing the
// whole packed byte buffer as one 2-D uint8 Func. decode() is never called --
// the TrustedInverse's decoder half owns dequantize.
class ExternQuantize : public Halide::Approximation {
public:
    explicit ExternQuantize(std::string extern_name)
        : extern_name_(std::move(extern_name)) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        return extern_quantize_blocks(std::move(inputs), extern_name_);
    }

    Halide::DecodeResult decode(std::vector<Halide::Func>) override {
        _halide_user_error << "ExternQuantize::decode is never valid -- it is only "
                              "the encoder half of a TrustedInverse.\n";
        return {};
    }

private:
    std::string extern_name_;
};

// codes(kk, blk) -> table[codes], a fixed int8 codebook lookup -- the shared
// codes->value step of every codebook-quantized format (IQ4_NL, MXFP4, TQ1_0,
// TQ2_0, NVFP4, IQ4_XS). Apply'd on the codes field between unpacking and the
// scale multiply, so ScaleDequant/TwoLevelScaleDequant see the looked-up value
// in place of a raw integer code. `table` is a Buffer over `static const`
// backing data (matching every per-format lookup_*() helper's idiom), copied
// around as a lightweight handle. encode() is the nearest-codeword search
// deferred to the extern, so it never runs.
class Codebook : public Halide::Approximation {
public:
    explicit Codebook(Halide::Buffer<int8_t> table)
        : table_(table) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "Codebook::encode is never valid -- the forward "
                              "codeword search is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func codes = encoded[0];
        Var kk("kk"), blk("blk");
        Buffer<int8_t> table = table_;
        // Clamp the index to the table's own extent -- a no-op on valid codes
        // (unpackers produce in-range indices), but it gives Halide's bounds
        // inference a provable index range across this Func boundary, instead
        // of falling back to the full int8 range (accessing table at -128).
        // The grid leaves clamp their grid index the same way.
        Func values("codebook_values");
        values(kk, blk) = table(clamp(cast<int32_t>(codes(kk, blk)), 0, table.dim(0).extent() - 1));
        return {{values}, {}};
    }

private:
    Halide::Buffer<int8_t> table_;
};

// {codes(kk, blk), scale} -> cast<float>(codes) * scale, the one-level scale
// multiply for the flat codebook formats. `codes` is the already-looked-up
// codebook value (see Codebook). `num_scales` is how many independent scale
// values the block has: 1 (IQ4_NL/MXFP4/TQ*, scale indexed by blk) or >1
// (NVFP4, scale indexed per sub-block kk / (block_size/num_scales)). This is
// SymmetricAffineQuantize::decode generalized with a sub-block scale index;
// the native symmetric formats keep their own invertible
// SymmetricAffineQuantize, so this is decode-only.
class ScaleDequant : public Halide::Approximation {
public:
    ScaleDequant(int block_size, int num_scales)
        : block_size_(block_size), num_scales_(num_scales) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "ScaleDequant::encode is never valid -- the forward "
                              "quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func codes = encoded[0], scale = encoded[1];
        Var kk("kk"), blk("blk");
        Func dequantized("scale_dequantized");
        if (num_scales_ == 1) {
            dequantized(kk, blk) = cast<float>(cast<int32_t>(codes(kk, blk))) * scale(blk);
        } else {
            int sub_size = block_size_ / num_scales_;
            dequantized(kk, blk) = cast<float>(cast<int32_t>(codes(kk, blk))) * scale(kk / sub_size, blk);
        }
        return {{dequantized}, {}};
    }

private:
    int block_size_, num_scales_;
};

// The K-quant / IQ4_XS two-level dequantize: a super-block-wide float `d`
// (and, for the affine K-quants, `dmin`) times a per-sub-block scale (and
// min). Inputs, in order: {d, [dmin,] scale, [min,] codes} -- `has_min`
// selects whether the dmin/min pair is present. `codes` may be raw integer
// codes (K-quants) or already-looked-up codebook values (IQ4_XS, via a
// Codebook stage). scale/min are indexed per sub-block (kk / sub_size).
// Decode-only, same rationale as ScaleDequant.
class TwoLevelScaleDequant : public Halide::Approximation {
public:
    TwoLevelScaleDequant(int sub_size, bool has_min)
        : sub_size_(sub_size), has_min_(has_min) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "TwoLevelScaleDequant::encode is never valid -- the "
                              "forward quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Var kk("kk"), blk("blk");
        Func dequantized("two_level_dequantized");
        if (has_min_) {
            Func d = encoded[0], dmin = encoded[1], scale = encoded[2], min = encoded[3], codes = encoded[4];
            dequantized(kk, blk) = d(blk) * cast<float>(scale(kk / sub_size_, blk)) * cast<float>(codes(kk, blk)) -
                                   dmin(blk) * cast<float>(min(kk / sub_size_, blk));
        } else {
            Func d = encoded[0], scale = encoded[1], codes = encoded[2];
            dequantized(kk, blk) = d(blk) * cast<float>(scale(kk / sub_size_, blk)) * cast<float>(codes(kk, blk));
        }
        return {{dequantized}, {}};
    }

private:
    int sub_size_;
    bool has_min_;
};

// ---------------------------------------------------------------------------
// 5. K-quants: combined-bit codes and per-sub-block (scale, min) packing.
// ---------------------------------------------------------------------------

// The invertible arithmetic behind GGML's K-quant "combined bit" codes: a
// wider-than-one-field code split into a low part and a high part, where
// `code = low + high*high_weight - offset` (verified by hand to collapse
// Q3_K's/Q5_K's/Q6_K's actual bit-OR reconstruction into one formula -- OR
// and + agree because the low/high bit ranges never overlap: high_weight is
// always the low part's own value range). Unlike the extern-delegated leaves
// in section 4, this is genuinely invertible in both directions, so it
// composes as an ordinary symmetric stage: decode() combines {low, high} ->
// code, encode() splits code -> {low, high}. The per-field packing (each part
// through its own PlanarBitPack/BytePack, then StructPack concatenating them
// in the format's on-disk order) is the composition around it -- e.g. for
// Q5_K, Compose{StructPack{{qs, qh}, order}, Apply{low_pack}, Apply{high_pack},
// CombineBits{...}} -- not this leaf, which is only the split/combine math.
class CombineBits : public Halide::Approximation {
public:
    CombineBits(int high_weight, int offset)
        : high_weight_(high_weight), offset_(offset) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];  // codes(kk, blk), the combined (pre-split) value
        Var kk("kk"), blk("blk");

        // `combined` is always >= 0 by construction (offset_ is exactly what
        // decode() subtracts after reconstructing low + high*weight), so the
        // %// below don't need to handle negative operands.
        Expr combined = cast<int32_t>(codes(kk, blk)) + offset_;
        Func low("combine_bits_low");
        low(kk, blk) = cast<int8_t>(combined % high_weight_);
        Func high("combine_bits_high");
        high(kk, blk) = cast<int8_t>(combined / high_weight_);
        return {{low, high}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func low = encoded[0], high = encoded[1];
        Var kk("kk"), blk("blk");
        Func code("combine_bits_code");
        code(kk, blk) = cast<int8_t>((cast<int32_t>(low(kk, blk)) + high_weight_ * cast<int32_t>(high(kk, blk))) - offset_);
        return {{code}, {}};
    }

private:
    int high_weight_, offset_;
};

// decode(bytes(sub, blk), 1 byte per sub-block) -> {scale(sub, blk),
// min(sub, blk)}, each the field's own nibble -- Q2_K's scale/min packing:
// unlike Q4_K/Q5_K's get_scale_min_k4 (below), each sub-block's (scale, min)
// pair lives in its own byte with no bit-interleaving across sub-blocks at
// all (low nibble = scale, high nibble = min).
class NibblePairPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0], min = inputs[1];
        Var sub("sub"), blk("blk");
        Func bytes("nibble_pair_pack_bytes");
        bytes(sub, blk) = cast<uint8_t>(cast<uint32_t>(scale(sub, blk)) | (cast<uint32_t>(min(sub, blk)) << 4));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var sub("sub"), blk("blk");
        Func scale("nibble_pair_pack_scale");
        scale(sub, blk) = cast<uint8_t>(bytes(sub, blk) & 0x0f);
        Func min("nibble_pair_pack_min");
        min(sub, blk) = cast<uint8_t>(cast<uint32_t>(bytes(sub, blk)) >> 4);
        return {{scale, min}, {}};
    }
};

// decode(bytes(byte_idx, blk), 12 bytes) -> {scale(sub, blk), min(sub, blk)}
// for sub in [0, 8) -- GGML's get_scale_min_k4 scheme, shared by Q4_K and
// Q5_K: for sub<4, scale/min are simply the low 6 bits of byte[sub]/
// byte[sub+4]; for sub>=4, each is a 4-bit low part from byte[sub+4]
// combined with a 2-bit high part borrowed from the top 2 bits of an
// earlier byte (byte[sub-4] for scale, byte[sub] for min) -- a
// bit-interleaved packing that fits 8 six-bit values into 6 bytes' worth of
// budget instead of 8 (see q4_k_generators.cpp's original header comment
// for the full derivation). encode() is the reverse of this -- never
// actually exercised (K-quant quantize is always extern-delegated), so it
// exists for interface completeness only, matching every other pack class
// in this file whose encode() only has a real caller for some formats.
class K4ScaleMinPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0], min = inputs[1];
        Var byte_idx("byte_idx"), blk("blk");

        auto sc = [&](Expr sub) -> Expr { return cast<uint32_t>(scale(sub, blk)); };
        auto mn = [&](Expr sub) -> Expr { return cast<uint32_t>(min(sub, blk)); };

        // byte[0..3] = scale[0..3] (low 6 bits); byte[4..7] = min[0..3] (low
        // 6 bits); byte[8..11] = min[jj]'s low 4 bits | scale[jj]'s low 4
        // bits (jj = byte_idx-8), plus each byte[jj]/byte[4+jj]'s top 2 bits
        // hold scale[4+jj]/min[4+jj]'s high 2 bits (added in below).
        Expr jj0 = clamp(byte_idx, 0, 3);
        Expr jj1 = clamp(byte_idx - 4, 0, 3);
        Expr jj2 = clamp(byte_idx - 8, 0, 3);
        Expr byte_0_3 = sc(jj0) & 0x3f;
        Expr byte_4_7 = mn(jj1) & 0x3f;
        Expr byte_8_11 = (sc(jj2 + 4) & 0x0f) | ((mn(jj2 + 4) & 0x0f) << 4);

        Func bytes("k4_scale_min_pack_bytes");
        Expr base = select(byte_idx < 4, byte_0_3, byte_idx < 8, byte_4_7, byte_8_11);
        // byte[0..3]'s top 2 bits hold scale[4..7]'s high 2 bits; byte[4..7]'s
        // top 2 bits hold min[4..7]'s high 2 bits.
        Expr with_high = select(byte_idx < 4, base | ((sc(byte_idx + 4) >> 4) << 6),
                                byte_idx < 8, base | ((mn(byte_idx) >> 4) << 6),
                                base);
        bytes(byte_idx, blk) = cast<uint8_t>(with_high);
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, 12)
        Var sub("sub"), blk("blk");

        Expr jj = clamp(sub - 4, 0, 3);
        Expr sc = select(sub < 4,
                         bytes(sub, blk) & 0x3f,
                         cast<uint8_t>((bytes(8 + jj, blk) & 0x0f) | ((bytes(jj, blk) >> 6) << 4)));
        Expr m = select(sub < 4,
                        bytes(sub + 4, blk) & 0x3f,
                        cast<uint8_t>((bytes(8 + jj, blk) >> 4) | ((bytes(4 + jj, blk) >> 6) << 4)));

        Func scale("k4_scale_min_pack_scale");
        scale(sub, blk) = sc;
        Func min("k4_scale_min_pack_min");
        min(sub, blk) = m;
        return {{scale, min}, {}};
    }
};

// decode(bytes(byte_idx, blk), 12 bytes) -> scale(sub, blk) for sub in
// [0, 16) -- Q3_K's 16 SIGNED 6-bit scale values (no min field), a
// different bit-interleaving than get_scale_min_k4 above: the 2 high bits
// always live in byte (sub%4)+8, at bit-shift 2*(sub/4); the 4 low bits
// live in byte (sub%8), taken from the byte's low nibble if sub<8 or high
// nibble if sub>=8. The final signed value is (low|(high<<4)) - 32 (see
// q3_k_generators.cpp's original header comment for the full derivation).
class Q3KScalePack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func scale = inputs[0];  // scale(sub, blk), signed in [-32, 31]
        Var byte_idx("byte_idx"), blk("blk");

        auto unsigned_at = [&](Expr sub) -> Expr {
            return cast<uint32_t>(cast<int32_t>(scale(sub, blk)) + 32);
        };

        // byte[0..7]'s nibbles hold the low 4 bits of scale[0..7]/scale[8..15].
        Expr low_lo = unsigned_at(clamp(byte_idx, 0, 7)) & 0x0f;
        Expr low_hi = unsigned_at(clamp(byte_idx, 0, 7) + 8) & 0x0f;
        Expr low_byte = low_lo | (low_hi << 4);

        // byte[8..11]'s 4 pairs of 2 bits each hold the high 2 bits of
        // scale[s]/scale[s+4]/scale[s+8]/scale[s+12] for s = byte_idx-8.
        Expr s = clamp(byte_idx - 8, 0, 3);
        Expr h0 = (unsigned_at(s) >> 4) & 0x3;
        Expr h1 = (unsigned_at(s + 4) >> 4) & 0x3;
        Expr h2 = (unsigned_at(s + 8) >> 4) & 0x3;
        Expr h3 = (unsigned_at(s + 12) >> 4) & 0x3;
        Expr high_byte = h0 | (h1 << 2) | (h2 << 4) | (h3 << 6);

        Func bytes("q3k_scale_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(select(byte_idx < 8, low_byte, high_byte));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, 12)
        Var sub("sub"), blk("blk");

        Expr low_byte_idx = sub % 8;
        Expr use_high_nibble = sub >= 8;
        Expr low_byte = bytes(low_byte_idx, blk);
        Expr low_val = select(use_high_nibble, cast<int32_t>(low_byte >> 4), cast<int32_t>(low_byte & 0x0f));
        Expr high_byte_idx = (sub % 4) + 8;
        Expr high_shift = (sub / 4) * 2;
        Expr high = cast<int32_t>((bytes(high_byte_idx, blk) >> high_shift) & 0x3);

        Func scale("q3k_scale_pack_scale");
        scale(sub, blk) = cast<int8_t>((low_val | (high << 4)) - 32);
        return {{scale}, {}};
    }
};

// ---------------------------------------------------------------------------
// 6. IQ2/IQ3 grid+sign codebook dequantize.
// ---------------------------------------------------------------------------
//
// Unlike IQ4_NL/MXFP4/TQ1_0/TQ2_0/NVFP4 above, these codebooks map one index
// to a whole *group* of 4 or 8 signed output bytes at once (GGML's published
// iq2s_grid/iq3xxs_grid/iq3s_grid tables, embedded verbatim from
// iq_grids_data.h), and each format combines its grid index, sign bits, and
// per-group scale via its own distinct bit layout -- there's no shared
// sub-formula across formats the way PlanarBitPack's instances turned out to
// be for the K-quants. Rather than force an
// artificial shared abstraction over 3 genuinely different bit layouts, each
// format below is its own small, decode-only Approximation leaf, wrapped by
// its make_*_scheme() factory in a TrustedInverse{ExternQuantize, Compose{...,
// BlockReshape}} (GGML's own reference quantizer for these runs a per-block
// codebook search -- see ggml_extern_quantize.cpp). decode() is a mechanical,
// verified-unchanged transcription of iq2_s_generators.cpp's/
// iq3_xxs_generators.cpp's/iq3_s_generators.cpp's own (already bit-exact)
// dequantize math, just reading from a `bytes(byte, blk)` Func instead of
// an `Input<Buffer<uint8_t, 2>>` directly, and producing block-indexed values
// (the composed BlockReshape does the flat<->block reshape) instead of a flat
// row itself. encode() traps: quantize is the ExternQuantize's job.

// IQ2_S: 256-element superblock, 8 groups of 32 elements, grid index = an 8-
// bit qs byte plus 2 extra high bits from a per-group qh byte (1024-entry,
// 64-bit iq2s_grid, 8 output bytes/index); signs stored directly (no
// ksigns_iq2xs indirection); scale a nibble byte array (2 groups/byte) via
// `d*(0.5+nibble)*0.25` -- {fp16 d; qs[32]; signs[32]; qh[8]; scales[8];},
// 82 bytes.
class IQ2SGridDequantize : public Halide::Approximation {
public:
    IQ2SGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq2s_grid, 1024, "iq2s_grid")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ2SGridDequantize is decode-only -- quantize is "
                              "deferred to an ExternQuantize via TrustedInverse.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 82)
        // Superblock structure recovered by the composed BlockReshape({8, 4, 8}):
        // j (element in [0,8)), l ([0,4)), ib32 (group in [0,8)).
        Var j("j"), l("l"), ib32("ib32"), blk("blk");

        constexpr int kQsOffset = 2;
        constexpr int kSignsOffset = kQsOffset + 256 / 8;    // 34
        constexpr int kQhOffset = kSignsOffset + 256 / 8;    // 66
        constexpr int kScalesOffset = kQhOffset + 256 / 32;  // 74

        Expr qs_l = bytes(kQsOffset + ib32 * 4 + l, blk);
        Expr qh_byte = cast<uint32_t>(bytes(kQhOffset + ib32, blk));
        Expr extra_bits = select(l == 0, (qh_byte << 8) & 0x300,
                                 l == 1, (qh_byte << 6) & 0x300,
                                 l == 2, (qh_byte << 4) & 0x300,
                                 (qh_byte << 2) & 0x300);
        Expr grid_idx_raw = cast<uint32_t>(qs_l) + extra_bits;
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(grid_idx_raw)), 0, 1023);

        Expr signs_byte = bytes(kSignsOffset + ib32 * 4 + l, blk);

        Expr scales_byte = bytes(kScalesOffset + ib32, blk);
        Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);

        Expr d = fp16_delta(bytes, 0, blk);
        Expr db = d * (0.5f + cast<float>(nibble)) * 0.25f;

        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j)) != 0;

        Func dequantized("iq2s_grid_dequantized");
        dequantized(j, l, ib32, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint64_t> grid_;
};

// IQ3_XXS: 256-element superblock, 8 groups of 32 elements, TWO grid indices
// per l (plain 8-bit qs bytes, no extra bits; 256-entry, 32-bit
// iq3xxs_grid, 4 output bytes/index); signs via the same ksigns_iq2xs
// indirection as IQ2_XXS (a 7-bit sign_idx and a 4-bit scale exponent both
// bit-packed into one little-endian uint32 "aux32" read from the scales-
// and-signs field); scale via `d*(0.5+exp)*0.5` -- {fp16 d; qs[64];
// scales_and_signs[32];}, 98 bytes.
class IQ3XXSGridDequantize : public Halide::Approximation {
public:
    IQ3XXSGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq3xxs_grid, 256, "iq3xxs_grid")),
          ksigns_(make_grid_buffer(iq_grids::ksigns_iq2xs, 128, "ksigns_iq2xs")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ3XXSGridDequantize is decode-only -- quantize is "
                              "deferred to an ExternQuantize via TrustedInverse.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 98)
        // Superblock structure recovered by the composed BlockReshape({8, 4, 8}):
        // j8 (element in [0,8)), l ([0,4)), ib32 (group in [0,8)).
        Var j8("j8"), l("l"), ib32("ib32"), blk("blk");
        Expr j4 = j8 % 4;    // byte within the 4-byte grid entry
        Expr half = j8 / 4;  // 0 (grid1) or 1 (grid2)

        constexpr int kQsOffset = 2;
        constexpr int kScalesSignsOffset = kQsOffset + 256 / 4;  // 66

        Expr grid_qs_idx = ib32 * 8 + l * 2 + half;
        Expr grid_idx = bytes(kQsOffset + grid_qs_idx, blk);

        Expr aux32 = le_u32(bytes, kScalesSignsOffset + ib32 * 4, blk);

        Expr d = fp16_delta(bytes, 0, blk);
        Expr db = d * (0.5f + cast<float>(aux32 >> 28)) * 0.5f;

        Expr sign_idx = cast<uint8_t>((aux32 >> (cast<uint32_t>(l) * 7)) & 127);
        Buffer<uint8_t> ksigns = ksigns_;
        Expr signs = ksigns(sign_idx);

        Buffer<uint32_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j8)) != 0;

        Func dequantized("iq3xxs_grid_dequantized");
        dequantized(j8, l, ib32, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint32_t> grid_;
    Halide::Buffer<uint8_t> ksigns_;
};

// IQ3_S: 256-element superblock, 8 groups of 32 elements, TWO grid indices
// per l (an 8-bit qs byte plus 1 extra high bit from a per-group qh byte,
// combined into a 9-bit index; 512-entry, 32-bit iq3s_grid, 4 output
// bytes/index); signs stored directly (no ksigns_iq2xs indirection, unlike
// IQ2_XXS/IQ3_XXS); scale a nibble byte array (one byte per *pair* of
// groups) via `d*(1+2*nibble)` (odd integers 1,3,...,31 -- not the
// "0.5+x*k" formula every other type here uses) -- {fp16 d; qs[64]; qh[8];
// signs[32]; scales[4];}, 110 bytes.
class IQ3SGridDequantize : public Halide::Approximation {
public:
    IQ3SGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq3s_grid, 512, "iq3s_grid")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ3SGridDequantize is decode-only -- quantize is "
                              "deferred to an ExternQuantize via TrustedInverse.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 110)
        // Superblock structure recovered by the composed BlockReshape({8, 4, 8}):
        // j8 (element in [0,8)), l ([0,4)), grp (group in [0,8)).
        Var j8("j8"), l("l"), grp("grp"), blk("blk");
        Expr j4 = j8 % 4;    // byte within the 4-byte grid entry
        Expr half = j8 / 4;  // 0 (first grid index of this l) or 1 (second)

        constexpr int kQsOffset = 2;
        constexpr int kQhOffset = kQsOffset + 256 / 4;         // 66
        constexpr int kSignsOffset = kQhOffset + 256 / 32;     // 74
        constexpr int kScalesOffset = kSignsOffset + 256 / 8;  // 106

        Expr qs_byte = bytes(kQsOffset + grp * 8 + l * 2 + half, blk);
        Expr qh_byte = cast<uint32_t>(bytes(kQhOffset + grp, blk));
        Expr bit_pos = l * 2 + half;
        Expr high_bit = (qh_byte >> cast<uint32_t>(bit_pos)) & 1;
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high_bit << 8))), 0, 511);

        Expr signs_byte = bytes(kSignsOffset + grp * 4 + l, blk);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j8)) != 0;

        Expr scales_byte = bytes(kScalesOffset + grp / 2, blk);
        Expr nibble = select((grp % 2) == 0, scales_byte & 0x0f, scales_byte >> 4);

        Expr d = fp16_delta(bytes, 0, blk);
        Expr db = d * (1.0f + 2.0f * cast<float>(nibble));

        Buffer<uint32_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);

        Func dequantized("iq3s_grid_dequantized");
        dequantized(j8, l, grp, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint32_t> grid_;
};

// decode({scales_h(2 bytes), scales_l(4 bytes)}) -> scale(sub, blk) for sub in
// [0, 8) -- IQ4_XS's per-sub-block 6-bit scale `ls`, already minus its 32 bias
// so it feeds TwoLevelScaleDequant's `d * scale(sub) * value` directly. `ls`
// is 4 low bits from scales_l (2 sub-blocks/byte) plus 2 high bits from
// scales_h (a little-endian uint16, 2 bits/sub-block) -- a two-field
// bit-interleaving, the peer of Q3KScalePack/K4ScaleMinPack but reading two
// separate byte fields. Decode-only: IQ4_XS quantize is extern-delegated.
class IQ4XSScalePack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ4XSScalePack::encode is never valid -- IQ4_XS "
                              "quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func scales_h = encoded[0], scales_l = encoded[1];
        Var sub("sub"), blk("blk");

        // ls = (scales_l[sub/2] >> 4*(sub%2)) & 0xf | ((scales_h >> 2*sub) & 3) << 4
        Expr low4 = cast<int32_t>((scales_l(sub / 2, blk) >> ((sub % 2) * 4)) & 0x0f);
        Expr sh_lo = cast<uint16_t>(scales_h(0, blk));
        Expr sh_hi = cast<uint16_t>(scales_h(1, blk));
        Expr sh = sh_lo | (sh_hi << 8);
        Expr high2 = cast<int32_t>((sh >> cast<uint16_t>(sub * 2)) & 3);
        Expr ls = low4 | (high2 << 4);

        Func scale("iq4xs_scale");
        scale(sub, blk) = cast<int8_t>(ls - 32);
        return {{scale}, {}};
    }
};

// The make_*() factories below each return one owned Halide::Approximation
// (as a std::unique_ptr<Halide::Approximation>, the framework's polymorphic
// scheme handle) -- a single leaf, a Compose, or a TrustedInverse, whichever
// the format actually is, never a single-element Compose wrapper. A Compose's
// stage 0 is outermost (its encoded output is the whole thing's result) and
// its last stage is innermost (closest to the original values).
//
// Native (in-Halide-quantizable) formats are a plain Compose. Extern-delegated
// formats -- whose forward quantize is an opaque GGML extern -- are a
// TrustedInverse pairing ExternQuantize (encode) with a Compose (decode); see
// Halide::TrustedInverse and section 4 above.

// quantize -> pack codes -> pack scale -> concatenate into one byte buffer
// with the scale stored first (matching every GGML block_* struct's
// `{fp16 d; ...qs;}` layout) -- everything a "block_qN_0-style" scheme needs
// *except* the flat<->block reshape, because this piece operates directly on
// already block-indexed (kk, blk) values. This is the part vec_dot reuses
// as-is (its Inputs are already block-indexed packed buffers, so there's
// nothing to reshape); make_symmetric_block_scheme() below adds BlockReshape
// on top of this for quantize_row/dequantize_row's flat 1-D array interface.
inline std::unique_ptr<Halide::Approximation> make_symmetric_block_codec(
    int block_size, int qmax, RoundingMode rounding, ScaleAnchor anchor, int code_bits) {
    using namespace Halide;
    // PlanarBitPack vs BytePack vs BitPack is a runtime choice, so it
    // can't be a plain value argument below like everything else --
    // Compose/Apply's constructors also accept an already-type-erased
    // std::unique_ptr<Approximation> for exactly this case.
    std::unique_ptr<Approximation> code_pack =
        code_bits == 4 ? std::unique_ptr<Approximation>(std::make_unique<PlanarBitPack>(4, 2, block_size / 2, qmax)) : code_bits == 1 ? std::unique_ptr<Approximation>(std::make_unique<BitPack>()) :
                                                                                                                                        std::unique_ptr<Approximation>(std::make_unique<BytePack>());
    int code_bytes = code_bits == 4 ? block_size / 2 : code_bits == 1 ? block_size / 8 :
                                                                        block_size;
    return std::make_unique<Compose>(
        StructPack{{2, code_bytes}, {1, 0}},
        Apply{1, 1, 1, Fp16Pack{}},
        Apply{0, 1, 1, std::move(code_pack)},
        SymmetricAffineQuantize{block_size, qmax, rounding, anchor});
}

// make_symmetric_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline std::unique_ptr<Halide::Approximation> make_symmetric_block_scheme(
    int block_size, int qmax, RoundingMode rounding, ScaleAnchor anchor, int code_bits) {
    return std::make_unique<Halide::Compose>(
        make_symmetric_block_codec(block_size, qmax, rounding, anchor, code_bits),
        BlockReshape{block_size});
}

// quantize -> pack codes -> pack scale -> pack min -> concatenate, scale
// then min then codes (matching block_q4_1/block_q5_1's `{fp16 d; fp16 m;
// ...qs;}` layout) -- the affine (min+scale) counterpart to
// make_symmetric_block_codec(), used by Q4_1 (code_bits=4, ClampedInt8). Q5_1
// pairs with make_affine_5bit_block_codec() below instead, since it also
// needs the qh high-bit field.
inline std::unique_ptr<Halide::Approximation> make_affine_block_codec(
    int block_size, int levels, AffineRounding rounding, int code_bits) {
    using namespace Halide;
    std::unique_ptr<Approximation> code_pack =
        code_bits == 4 ? std::unique_ptr<Approximation>(std::make_unique<PlanarBitPack>(4, 2, block_size / 2, /*qmax=*/0)) : std::unique_ptr<Approximation>(std::make_unique<BytePack>());
    return std::make_unique<Compose>(
        StructPack{{2, 2, code_bits == 4 ? block_size / 2 : block_size}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},            // pack min
        Apply{1, 1, 1, Fp16Pack{}},            // pack scale
        Apply{0, 1, 1, std::move(code_pack)},  // pack codes
        AffineQuantize{block_size, levels, rounding});
}

// make_affine_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline std::unique_ptr<Halide::Approximation> make_affine_block_scheme(
    int block_size, int levels, AffineRounding rounding, int code_bits) {
    return std::make_unique<Halide::Compose>(
        make_affine_block_codec(block_size, levels, rounding, code_bits),
        BlockReshape{block_size});
}

// Symmetric quantize (like make_symmetric_block_codec()) but 5-bit: the
// extra 5th bit goes through FiveBitPack instead of PlanarBitPack, producing
// an extra `qh` field between the scale and the nibble bytes in the output
// layout (matching block_q5_0's `{fp16 d; qh[4]; qs[16];}`). `qmax` is
// always 16 (5-bit signed range [-16, 15]).
inline std::unique_ptr<Halide::Approximation> make_symmetric_5bit_block_codec(int block_size, int qmax) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 4, block_size / 2}, {2, 1, 0}},
        Apply{2, 1, 1, Fp16Pack{}},
        Apply{0, 1, 2, FiveBitPack{block_size, qmax}},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::TruncateHalfUpWithOffset, ScaleAnchor::ExtremeSignedValue});
}

// make_symmetric_5bit_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline std::unique_ptr<Halide::Approximation> make_symmetric_5bit_block_scheme(int block_size, int qmax) {
    return std::make_unique<Halide::Compose>(
        make_symmetric_5bit_block_codec(block_size, qmax),
        BlockReshape{block_size});
}

// Affine quantize (like make_affine_block_codec()) but 5-bit: same
// FiveBitPack addition as make_symmetric_5bit_block_codec(), plus the min
// field affine quantization needs, matching block_q5_1's
// `{fp16 d; fp16 m; qh[4]; qs[16];}`. `qmax=0` when calling FiveBitPack
// here (unlike Q5_0's 16): AffineQuantize's codes are already unsigned
// [0, levels], not centered, so there's no offset to re-apply before
// splitting into nibble+high-bit.
inline std::unique_ptr<Halide::Approximation> make_affine_5bit_block_codec(int block_size, int levels, AffineRounding rounding) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 2, 4, block_size / 2}, {2, 3, 1, 0}},
        Apply{3, 1, 1, Fp16Pack{}},                           // pack min
        Apply{2, 1, 1, Fp16Pack{}},                           // pack scale
        Apply{0, 1, 2, FiveBitPack{block_size, /*qmax=*/0}},  // pack codes
        AffineQuantize{block_size, levels, rounding});
}

// make_affine_5bit_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline std::unique_ptr<Halide::Approximation> make_affine_5bit_block_scheme(int block_size, int levels, AffineRounding rounding) {
    return std::make_unique<Halide::Compose>(
        make_affine_5bit_block_codec(block_size, levels, rounding),
        BlockReshape{block_size});
}

// Symmetric byte-packed quantize (like make_symmetric_block_codec() with
// code_bits=8) plus AppendCodeSum's derived 's' field, matching
// block_q8_1's `{fp16 d; fp16 s; qs[32];}` -- Q8_1's scheme. Q8_1 is
// activation-only (GGML has no public to_float for it), so there's normally
// no dequantize_row Generator for this scheme's flat-array variant below --
// but its decode() is still correct and used by any vec_dot pairing against
// Q8_1 as the activation format. AppendCodeSum needs no Apply wrapper: it
// consumes and produces the *whole* current list (like quantize itself),
// not just one element of it.
inline std::unique_ptr<Halide::Approximation> make_symmetric_byte_sum_block_codec(int block_size, int qmax) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 2, block_size}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},  // pack sum
        Apply{1, 1, 1, Fp16Pack{}},  // pack scale
        Apply{0, 1, 1, BytePack{}},  // pack codes
        AppendCodeSum{block_size},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::Nearest, ScaleAnchor::AbsMax});
}

// make_symmetric_byte_sum_block_codec() + BlockReshape: the full flat-array
// quantize_row scheme (see above -- there is normally no matching
// dequantize_row Generator, but this is still available for completeness/
// consistency with every other scheme here).
inline std::unique_ptr<Halide::Approximation> make_symmetric_byte_sum_block_scheme(int block_size, int qmax) {
    return std::make_unique<Halide::Compose>(
        make_symmetric_byte_sum_block_codec(block_size, qmax),
        BlockReshape{block_size});
}

// Q8_K: activation-only (quantize_row only, matching Q8_1's own situation
// above -- see q8_k_generators.cpp), one 256-element superblock, plain int8
// codes (BytePack), one float32 (not fp16) scale (F32Pack), and 16
// per-group int32-then-int16 sums (AppendGroupSumsInt16) -- {float d;
// qs[256]; bsums[16];}, 292 bytes. RoundingMode::NearestEvenClampedHigh/
// ScaleAnchor::ExtremeSignedValueTwoStep reproduce GGML's exact
// nearest-int-then-reciprocal-pair quantizer bit-for-bit -- see their own
// comments in SymmetricAffineQuantize above for why the usual
// Nearest/ExtremeSignedValue formulas aren't equivalent here.
inline std::unique_ptr<Halide::Approximation> make_q8_k_codec(int block_size, int qmax) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{4, block_size, (block_size / 16) * 2}, {1, 0, 2}},
        Apply{2, 1, 1, Int16Pack{}},  // pack bsums
        Apply{1, 1, 1, F32Pack{}},    // pack scale
        Apply{0, 1, 1, BytePack{}},   // pack codes
        AppendGroupSumsInt16{16},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::NearestEvenClampedHigh,
                                ScaleAnchor::ExtremeSignedValueTwoStep});
}

inline std::unique_ptr<Halide::Approximation> make_q8_k_scheme(int block_size, int qmax) {
    return std::make_unique<Halide::Compose>(
        make_q8_k_codec(block_size, qmax),
        BlockReshape{block_size});
}

// ---------------------------------------------------------------------------
// Factory helpers for the shared extern-delegated shapes. Each is a plain
// function that assembles a TrustedInverse{ExternQuantize, Compose{...}} out
// of the section-4 leaves -- transparent (it returns exactly the Compose you'd
// write by hand), unlike the bespoke Approximation subclasses this file used
// to have. The canonical composition for each family lives here once; the
// per-format make_*_scheme() below are just its parameters.
// ---------------------------------------------------------------------------

// Codebook formats (IQ4_NL/MXFP4/TQ2_0/TQ1_0/NVFP4): unpack the code field,
// look it up in `table`, unpack the scale field, one-level scale multiply,
// reshape. `scale_first` is the on-disk field order ({scale; codes} vs
// {codes; scale}); StructPack normalizes both to logical {codes, scale}.
inline std::unique_ptr<Halide::Approximation> make_codebook_scheme(
    std::string extern_name, int block_size, Halide::Buffer<int8_t> table,
    std::unique_ptr<Halide::Approximation> code_pack, int code_bytes,
    std::unique_ptr<Halide::Approximation> scale_pack, int scale_bytes,
    int num_scales, bool scale_first) {
    using namespace Halide;
    StructPack layout = scale_first ? StructPack{{scale_bytes, code_bytes}, {1, 0}} : StructPack{{code_bytes, scale_bytes}, {0, 1}};
    return std::make_unique<TrustedInverse>(
        ExternQuantize{std::move(extern_name)},
        Compose{
            std::move(layout),
            Apply{0, 1, 1, std::move(code_pack)},        // code bytes -> codes
            Apply{0, 1, 1, Codebook{std::move(table)}},  // codes -> codebook values
            Apply{1, 1, 1, std::move(scale_pack)},       // scale bytes -> scale
            ScaleDequant{block_size, num_scales},
            BlockReshape{block_size},
        });
}

// K-quant formats (Q2_K/Q3_K/Q4_K/Q5_K/Q6_K): unpack d (and, when has_min,
// dmin), the per-sub-block (scale[, min]) via `scale_min_pack`, and the codes
// via `code_pack`, then the two-level scale multiply and reshape.
// `field_widths`/`input_index` are StructPack's on-disk field order,
// normalized to logical {d[, dmin], scale_min, code}. This is the old
// KQuantDequantize parameter list -- now assembling a Compose, not a class.
inline std::unique_ptr<Halide::Approximation> make_k_quant_scheme(
    std::string extern_name, int block_size, int sub_size,
    std::vector<int> field_widths, std::vector<int> input_index, bool has_min,
    std::unique_ptr<Halide::Approximation> scale_min_pack,
    std::unique_ptr<Halide::Approximation> code_pack) {
    using namespace Halide;
    Compose decode = has_min ? Compose{
                                   StructPack{field_widths, input_index},
                                   Apply{0, 1, 1, Fp16Pack{}},                 // d
                                   Apply{1, 1, 1, Fp16Pack{}},                 // dmin
                                   Apply{2, 2, 1, std::move(scale_min_pack)},  // scale_min -> {scale, min}
                                   Apply{4, 1, 1, std::move(code_pack)},       // codes
                                   TwoLevelScaleDequant{sub_size, true},
                                   BlockReshape{block_size},
                               } :
                               Compose{
                                   StructPack{field_widths, input_index},
                                   Apply{0, 1, 1, Fp16Pack{}},                 // d
                                   Apply{1, 1, 1, std::move(scale_min_pack)},  // scale
                                   Apply{2, 1, 1, std::move(code_pack)},       // codes
                                   TwoLevelScaleDequant{sub_size, false},
                                   BlockReshape{block_size},
                               };
    return std::make_unique<TrustedInverse>(ExternQuantize{std::move(extern_name)}, std::move(decode));
}

// IQ grid formats (IQ2_S/IQ3_XXS/IQ3_S): a bespoke grid+sign+scale decode leaf
// that emits values in the superblock's nested structure, with
// BlockReshape(`block_extents`) doing the flat<->block reshape.
inline std::unique_ptr<Halide::Approximation> make_grid_scheme(
    std::string extern_name, std::unique_ptr<Halide::Approximation> grid_leaf,
    std::vector<int> block_extents) {
    using namespace Halide;
    return std::make_unique<TrustedInverse>(
        ExternQuantize{std::move(extern_name)},
        Compose{std::move(grid_leaf), BlockReshape{std::move(block_extents)}});
}

// IQ4_NL: 32-element blocks, 4-bit codes into a 16-value non-uniform
// codebook, one fp16 scale per block -- {fp16 d; qs[16];}, 18 bytes. Extern
// quantize; decode unpacks {code_bytes, scale_bytes} (ScaleFirst), looks the
// nibbles up in the codebook, applies the one fp16 scale, and reshapes to a
// flat row.
inline std::unique_ptr<Halide::Approximation> make_iq4_nl_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl");
    return make_codebook_scheme("iq4_nl_quantize_via_ggml", 32, table,
                                std::make_unique<PlanarBitPack>(4, 2, 16, 0), 16,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/true);
}

// MXFP4: 32-element blocks, 4-bit codes into the same-shaped 16-value
// codebook as IQ4_NL (different values), one E8M0 (1-byte, power-of-two)
// scale per block -- {e8m0 e; qs[16];}, 17 bytes.
inline std::unique_ptr<Halide::Approximation> make_mxfp4_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_mxfp4");
    return make_codebook_scheme("mxfp4_quantize_via_ggml", 32, table,
                                std::make_unique<PlanarBitPack>(4, 2, 16, 0), 16,
                                std::make_unique<E8M0Pack>(), 1, 1, /*scale_first=*/true);
}

// TQ2_0: 256-element superblock, 2-bit codes (each in {0,1,2}, meaning
// {-1,0,1}) via PlanarBitPack(2, 4, 32, 0)'s window-interleaved layout, one
// fp16 scale -- {qs[64]; fp16 d;}, 66 bytes -- qs *before* d, unlike most
// formats here (StructPack's codes-first field order below).
inline std::unique_ptr<Halide::Approximation> make_tq2_0_scheme() {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 4, "kvalues_tq2_0");
    return make_codebook_scheme("tq2_0_quantize_via_ggml", 256, table,
                                std::make_unique<PlanarBitPack>(2, 4, 32, 0), 64,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/false);
}

// TQ1_0: 256-element superblock, base-3 codes (each in {0,1,2}, meaning
// {-1,0,1}) via TritPack's 5-trits/byte (+4-trits/byte tail) packing, one
// fp16 scale -- {qs[48]; qh[4]; fp16 d;}, 54 bytes -- qs+qh (combined, 52
// bytes) *before* d, like TQ2_0. Reuses TQ2_0's exact {-1, 0, 1, unused}
// codebook (TritPack's codes are the same raw 0/1/2 digit either way).
inline std::unique_ptr<Halide::Approximation> make_tq1_0_scheme() {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 4, "kvalues_tq1_0");
    return make_codebook_scheme("tq1_0_quantize_via_ggml", 256, table,
                                std::make_unique<TritPack>(), 52,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/false);
}

// NVFP4: 64-element block, 4 sub-blocks of 16 elements each, 4-bit codes
// into the same 16-value codebook MXFP4 uses (NVFP4 is MXFP4 with
// finer-grained scales), one UE4M3 scale *per sub-block* via UE4M3Pack --
// {d[4]; qs[32];}, 36 bytes -- ScaleDequant's num_scales=4 (not 1) is what
// makes each sub-block's dequantize use its own scale byte instead of one
// shared scale for the whole 64-element block.
inline std::unique_ptr<Halide::Approximation> make_nvfp4_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_nvfp4");
    return make_codebook_scheme("nvfp4_quantize_via_ggml", 64, table,
                                std::make_unique<PlanarBitPack>(4, 2, 8, 0), 32,
                                std::make_unique<UE4M3Pack>(), 4, /*num_scales=*/4, /*scale_first=*/true);
}

// Q4_K: 256-element superblock, 8 sub-blocks of 32 elements each, plain
// 4-bit codes (PlanarBitPack(4, 2, 32, 0)) and get_scale_min_k4-packed
// (scale, min) pairs (K4ScaleMinPack) -- {fp16 d; fp16 dmin; scales[12];
// qs[128];}, 144 bytes, fields already in {d, dmin, scale_min, code} logical
// order. Two-level scale (d*scale(sub)*code - dmin*min(sub)).
inline std::unique_ptr<Halide::Approximation> make_q4_k_scheme() {
    using namespace Halide;
    return make_k_quant_scheme("q4_k_quantize_via_ggml", 256, 32, {2, 2, 12, 128}, {0, 1, 2, 3}, true,
                               std::make_unique<K4ScaleMinPack>(),
                               std::make_unique<PlanarBitPack>(4, 2, 32, 0));
}

// Q5_K: same super-block/sub-block/scale-min shape as Q4_K, but each code
// is 5 bits: a plain 4-bit low nibble (PlanarBitPack(4, 2, 32, 0)) plus a
// 5th high bit from a separate 32-byte, 8-window rotating-bit array
// (PlanarBitPack(1, 8, 32, 0)) -- {fp16 d; fp16 dmin; scales[12]; qh[32];
// qs[128];}, 176 bytes. qh+qs are adjacent in memory, treated as one
// 160-byte combined "code" field, split by an inner Compose {StructPack ->
// unpack qh/qs -> CombineBits} (HighFirst: qh before qs), offset=0 since
// Q5_K's code is a plain 0..31 unsigned magnitude, not recentered.
inline std::unique_ptr<Halide::Approximation> make_q5_k_scheme() {
    using namespace Halide;
    return make_k_quant_scheme(
        "q5_k_quantize_via_ggml", 256, 32, {2, 2, 12, 160}, {0, 1, 2, 3}, true,
        std::make_unique<K4ScaleMinPack>(),
        // Combined 5-bit code: qh (high bit) + qs (low nibble), split by an
        // inner Compose. HighFirst (qh before qs); offset 0 (plain 0..31).
        std::make_unique<Compose>(
            StructPack{{32, 128}, {1, 0}},               // {qh[32]; qs[128];} -> {low, high}
            Apply{0, 1, 1, PlanarBitPack{4, 2, 32, 0}},  // qs -> low nibble
            Apply{1, 1, 1, PlanarBitPack{1, 8, 32, 0}},  // qh -> high bit
            CombineBits{16, 0}));
}

// Q2_K: 256-element superblock, 16 sub-blocks of 16 elements each, plain
// 2-bit codes (PlanarBitPack(2, 4, 32, 0)) and independent per-sub-block
// nibble-pair (scale, min) (NibblePairPack, no bit-interleaving) --
// {scales[16]; qs[64]; fp16 d; fp16 dmin;}, 84 bytes, fields on-disk in
// {scale_min, code, d, dmin} order (scale_min/code *before* d/dmin, unlike
// Q4_K/Q5_K); StructPack's input_index normalizes back to {d, dmin,
// scale_min, code}.
inline std::unique_ptr<Halide::Approximation> make_q2_k_scheme() {
    using namespace Halide;
    return make_k_quant_scheme("q2_k_quantize_via_ggml", 256, 16, {16, 64, 2, 2}, {2, 3, 0, 1}, true,
                               std::make_unique<NibblePairPack>(),
                               std::make_unique<PlanarBitPack>(2, 4, 32, 0));
}

// Q3_K: 256-element superblock, 16 sub-blocks of 16 elements each, no min
// (symmetric, not affine) -- each code is 3 bits: 2 low bits
// (PlanarBitPack(2, 4, 32, 0)) plus a high bit from a 32-byte, 8-window
// rotating-bit "hmask" array (PlanarBitPack(1, 8, 32, 0)), recentered by -4
// (CombineBits offset=4, matching a signed [-4, 3] range); scale is 16
// SIGNED 6-bit values, its own bit-interleaving distinct from get_scale_min_k4
// (Q3KScalePack) -- {hmask[32]; qs[64]; scales[12]; fp16 d;}, 110 bytes.
// hmask+qs are adjacent in memory, treated as one 96-byte combined "code"
// field (HighFirst: hmask before qs); on-disk {code, scale, d} normalizes to
// logical {d, scale, code}.
inline std::unique_ptr<Halide::Approximation> make_q3_k_scheme() {
    using namespace Halide;
    return make_k_quant_scheme(
        "q3_k_quantize_via_ggml", 256, 16, {96, 12, 2}, {2, 1, 0}, false,
        std::make_unique<Q3KScalePack>(),
        // Combined 3-bit code: hmask (high bit) + qs (low 2 bits). HighFirst;
        // offset 4 (recenters to signed [-4, 3]).
        std::make_unique<Compose>(
            StructPack{{32, 64}, {1, 0}},                // {hmask[32]; qs[64];} -> {low, high}
            Apply{0, 1, 1, PlanarBitPack{2, 4, 32, 0}},  // qs -> low 2 bits
            Apply{1, 1, 1, PlanarBitPack{1, 8, 32, 0}},  // hmask -> high bit
            CombineBits{4, 4}));
}

// Q6_K: 256-element superblock, 16 sub-blocks of 16 elements each, no min --
// each code is 6 bits: a plain 4-bit low nibble over *two* 128-element
// halves (PlanarBitPack(4, 2, 64, 0)) plus 2 high bits
// (PlanarBitPack(2, 4, 32, 0)), recentered by -32; scale is 16 plain SIGNED
// int8 values, no bit-interleaving at all (BytePack -- its plain
// reinterpret<int8_t> is exactly what this needs) -- {ql[128]; qh[64];
// scales[16]; fp16 d;}, 210 bytes. ql+qh are adjacent in memory, treated as
// one 192-byte combined "code" field (LowFirst: ql before qh).
inline std::unique_ptr<Halide::Approximation> make_q6_k_scheme() {
    using namespace Halide;
    return make_k_quant_scheme(
        "q6_k_quantize_via_ggml", 256, 16, {192, 16, 2}, {2, 1, 0}, false,
        std::make_unique<BytePack>(),  // 16 signed int8 scales
        // Combined 6-bit code: ql (low nibble) + qh (high 2 bits). LowFirst;
        // offset 32 (recenters to signed [-32, 31]).
        std::make_unique<Compose>(
            StructPack{{128, 64}, {0, 1}},               // {ql[128]; qh[64];} -> {low, high}
            Apply{0, 1, 1, PlanarBitPack{4, 2, 64, 0}},  // ql -> low nibble
            Apply{1, 1, 1, PlanarBitPack{2, 4, 32, 0}},  // qh -> high 2 bits
            CombineBits{16, 32}));
}

// IQ2_S/IQ3_XXS/IQ3_S: see IQ2SGridDequantize/IQ3XXSGridDequantize/
// IQ3SGridDequantize above for the bit-layout rationale -- each is a bespoke,
// self-contained grid+sign+scale decode leaf (its three bit layouts share no
// sub-formula worth abstracting). Extern quantize; the grid leaf's decode
// produces block-indexed values, and BlockReshape composes the flat<->block
// reshape on top.
inline std::unique_ptr<Halide::Approximation> make_iq2_s_scheme() {
    return make_grid_scheme("iq2_s_quantize_via_ggml", std::make_unique<IQ2SGridDequantize>(), {8, 4, 8});
}

inline std::unique_ptr<Halide::Approximation> make_iq3_xxs_scheme() {
    return make_grid_scheme("iq3_xxs_quantize_via_ggml", std::make_unique<IQ3XXSGridDequantize>(), {8, 4, 8});
}

inline std::unique_ptr<Halide::Approximation> make_iq3_s_scheme() {
    return make_grid_scheme("iq3_s_quantize_via_ggml", std::make_unique<IQ3SGridDequantize>(), {8, 4, 8});
}

// IQ4_XS: 256-element superblock, 8 sub-blocks of 32 elements, the superblock
// generalization of IQ4_NL's fixed 16-value codebook -- plain 4-bit codes
// (PlanarBitPack(4, 2, 16, 0)) into the same kvalues_iq4nl table, scaled by
// `d * (ls - 32)`, a two-level scale (no min) whose per-sub-block `ls` is
// bit-interleaved across two byte fields (IQ4XSScalePack) -- {fp16 d;
// scales_h[2]; scales_l[4]; qs[128];}, 136 bytes.
inline std::unique_ptr<Halide::Approximation> make_iq4_xs_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl_xs");
    return std::make_unique<TrustedInverse>(
        ExternQuantize{"iq4_xs_quantize_via_ggml"},
        Compose{
            StructPack{{2, 2, 4, 128}, {0, 1, 2, 3}},    // -> {d, scales_h, scales_l, qs}
            Apply{0, 1, 1, Fp16Pack{}},                  // d
            Apply{1, 1, 2, IQ4XSScalePack{}},            // {scales_h, scales_l} -> scale(sub)
            Apply{2, 1, 1, PlanarBitPack{4, 2, 16, 0}},  // qs -> nibbles
            Apply{2, 1, 1, Codebook{table}},             // nibbles -> codebook values
            TwoLevelScaleDequant{32, false},
            BlockReshape{256},
        });
}

}  // namespace ggml_halide
