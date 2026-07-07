#pragma once

// Reusable Approximation components for GGML-style per-block quantized
// weight formats -- see doc/ApproximationDesign.md and the plan this file
// implements for the rationale. Every weight format is built by composing
// these kinds of pieces via Halide::Compose/Halide::Apply into a scheme
// (see the make_*_scheme()/make_*_codec() factory functions below), which
// symmetric_quant_generators.cpp then splices into a Generator via
// Func::approximate_by()/Pipeline::compute_offline() -- never by calling
// Approximation::encode()/decode() directly.
//
//   1. BlockReshape -- lossless relayout: flat values <-> (kk, blk).
//   2. SymmetricAffineQuantize/AffineQuantize -- the actual lossy step:
//      block values <-> (integer codes, one or two float(s) per block).
//   3a. Fp16Pack/NibblePack/BytePack/FiveBitPack -- per-field bit packing:
//      a typed field (codes, scale, min) <-> its own on-disk byte encoding.
//   3b. AppendCodeSum -- a derived extra field, computed from other
//      already-encoded fields rather than from the original values.
//   3c. StructPack -- concatenates N already-packed fields into one
//      byte-addressed buffer, matching a specific on-disk block layout
//      (e.g. block_q4_0).
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

// packed(kk, blk) = flat(blk*block_size + kk). Reused unchanged later for
// the interleaved multi-column repack layouts (see the plan's roadmap).
class BlockReshape : public Halide::Approximation {
public:
    explicit BlockReshape(int block_size)
        : block_size_(block_size) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Var kk("kk"), blk("blk");
        Func packed("block_reshape_packed");
        packed(kk, blk) = flat(blk * block_size_ + kk);
        return {{packed}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func packed = encoded[0];
        Var k("k");
        Func flat("block_reshape_unpacked");
        flat(k) = packed(k % block_size_, k / block_size_);
        return {{flat}, {}};
    }

private:
    int block_size_;
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
        Expr bits = cast<uint32_t>(bytes(0, blk)) | (cast<uint32_t>(bytes(1, blk)) << 8) |
                    (cast<uint32_t>(bytes(2, blk)) << 16) | (cast<uint32_t>(bytes(3, blk)) << 24);
        Func scale("f32_pack_scale");
        scale(blk) = reinterpret<float>(bits);
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
// deferring quantize to GGML's own reference (see LookupTableQuantize
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
// LookupTableQuantize's `num_scales` > 1: the input/output Funcs here are
// already indexed by `sub` directly (one byte each, no further byte-within-
// field splitting needed, unlike Fp16Pack's 2-byte value). encode() is
// log2-based and therefore *not* bit-exact -- matching every other format's
// rationale for deferring quantize to GGML's own reference (see
// LookupTableQuantize above); it exists for interface completeness only,
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

// encode(codes(kk, blk) signed in [-qmax, qmax-1]) -> block_size/2 bytes;
// decode reverses it. Packs codes[b] into the low nibble and codes[b+half]
// into the high nibble of byte b -- GGML's actual convention for every
// nibble-packed format (Q4_0, Q4_1, Q5_0's low bits, Q4_K, ...), not just
// the more obvious "adjacent pair per byte" layout.
class NibblePack : public Halide::Approximation {
public:
    NibblePack(int block_size, int qmax)
        : block_size_(block_size), qmax_(qmax) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var b("b"), blk("blk");
        int half = block_size_ / 2;
        Expr lo = cast<uint8_t>(cast<int32_t>(codes(b, blk)) + qmax_);
        Expr hi = cast<uint8_t>(cast<int32_t>(codes(b + half, blk)) + qmax_);
        Func bytes("nibble_pack_bytes");
        bytes(b, blk) = cast<uint8_t>(lo | (hi << 4));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(b, blk), b in [0, block_size/2)
        Var kk("kk"), blk("blk");
        int half = block_size_ / 2;
        Expr b = kk % half;
        Expr is_low = kk < half;
        Expr byte = bytes(b, blk);
        Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);
        Func codes("nibble_pack_codes");
        codes(kk, blk) = cast<int8_t>(cast<int32_t>(nibble) - qmax_);
        return {{codes}, {}};
    }

private:
    int block_size_, qmax_;
};

// Like NibblePack, but the low/high-nibble split is per `window_size`-
// element sub-block instead of spanning the whole `num_windows*window_size`
// block -- GGML's NVFP4 layout (4 independent 16-element sub-blocks, each
// packing its own 8 bytes: byte `sub*window_size/2 + j` holds sub-block
// `sub`'s elements `j` (low nibble) and `j + window_size/2` (high nibble),
// not a low/high split across the *whole* 64-element block the way
// NibblePack alone would assume). NibblePack itself is the degenerate
// `num_windows == 1` case of this.
class SubBlockNibblePack : public Halide::Approximation {
public:
    // `num_windows` isn't needed by the formulas below (bounds propagate
    // backward from the caller, like everywhere else in this file) -- it's
    // accepted purely so call sites document both halves of the block
    // shape, matching TwoBitPack/TritPack's fully-parameterized siblings.
    SubBlockNibblePack(int window_size, int num_windows, int qmax)
        : window_size_(window_size), qmax_(qmax) {
        (void)num_windows;
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var byte_idx("byte_idx"), blk("blk");
        int half = window_size_ / 2;
        Expr window = byte_idx / half;
        Expr b_local = byte_idx % half;
        Expr kk_lo = window * window_size_ + b_local;
        Expr kk_hi = kk_lo + half;
        Expr lo = cast<uint8_t>(cast<int32_t>(codes(kk_lo, blk)) + qmax_);
        Expr hi = cast<uint8_t>(cast<int32_t>(codes(kk_hi, blk)) + qmax_);
        Func bytes("sub_block_nibble_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(lo | (hi << 4));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, num_windows*window_size/2)
        Var kk("kk"), blk("blk");
        int half = window_size_ / 2;
        Expr window = kk / window_size_;
        Expr local = kk % window_size_;
        Expr b_local = local % half;
        Expr is_low = local < half;
        Expr byte_idx = window * half + b_local;
        Expr byte = bytes(byte_idx, blk);
        Expr nibble = select(is_low, byte & 0x0f, (byte >> 4) & 0x0f);
        Func codes("sub_block_nibble_pack_codes");
        codes(kk, blk) = cast<int8_t>(cast<int32_t>(nibble) - qmax_);
        return {{codes}, {}};
    }

private:
    int window_size_, qmax_;
};

// encode(codes(kk, blk) signed in [-qmax, qmax-1]) -> {nibble_bytes(b, blk)
// block_size/2 bytes, qh_bytes(qb, blk) 4 bytes}; decode reverses it. Splits
// each 5-bit value into a 4-bit low nibble (packed exactly like NibblePack)
// and a 5th (0x10) high bit, OR-accumulated one bit per element into a
// 32-bit little-endian word -- GGML's Q5_0/Q5_1 layout. Verified by hand
// that qh's bit `kk` is element kk's own high bit (GGML's own code computes
// this via a low/high-half split for scalar-loop efficiency -- e.g.
// `(qh >> (byte_idx+12)) & 0x10` for the high half -- but that's an
// equivalent, more roundabout way of writing the same "bit kk of qh is
// element kk's high bit" fact used directly here). Like NibblePack, `qmax`
// re-centers already-decoded codes before splitting -- 16 for Q5_0's
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

        Expr qh = cast<uint32_t>(qh_bytes(0, blk)) |
                  (cast<uint32_t>(qh_bytes(1, blk)) << 8) |
                  (cast<uint32_t>(qh_bytes(2, blk)) << 16) |
                  (cast<uint32_t>(qh_bytes(3, blk)) << 24);
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
// identity-shaped case NibblePack doesn't cover, used by e.g. Q8_0). Unlike
// NibblePack, this formula has no precondition on kk's range (reinterpret is
// valid for any kk) -- it doesn't know or care what block_size is; bounds
// propagate backward from whatever actually consumes it.
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

// encode(codes(kk, blk) unsigned in [0, 4)) -> block_size/4 bytes; decode
// reverses it. GGML's TQ2_0 layout: not a plain "4 consecutive elements per
// byte" packing -- the block is split into 2 equal windows of block_size/2
// elements each, and within a window, byte `m` holds one 2-bit "plane" per
// element at `m, m + window_bytes, m + 2*window_bytes, m + 3*window_bytes`
// (window_bytes = block_size/8), i.e. 4 elements *window_bytes apart*, not
// adjacent -- verified by hand against tq2_0_generators.cpp's original
// dequantize math (`half = gi/half_block; l = local/window_bytes; m =
// local%window_bytes; byte_idx = half*window_bytes + m; shift = l*2`).
// `qmax` re-centers already-decoded codes before splitting, like NibblePack
// (0 when used as a LookupTableQuantize code_pack, since the raw 2-bit value
// itself is the table index).
class TwoBitPack : public Halide::Approximation {
public:
    TwoBitPack(int block_size, int qmax)
        : block_size_(block_size), qmax_(qmax) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];
        Var byte_idx("byte_idx"), blk("blk");
        int half_block = block_size_ / 2;
        int window_bytes = block_size_ / 8;

        auto unsigned_at = [&](Expr k) -> Expr {
            return cast<uint8_t>(cast<int32_t>(codes(k, blk)) + qmax_);
        };

        // byte_idx in [0, block_size/4): half = byte_idx / window_bytes,
        // m = byte_idx % window_bytes.
        Expr half = byte_idx / window_bytes;
        Expr m = byte_idx % window_bytes;
        Expr base = half * half_block + m;
        Expr b0 = unsigned_at(base);
        Expr b1 = unsigned_at(base + window_bytes);
        Expr b2 = unsigned_at(base + 2 * window_bytes);
        Expr b3 = unsigned_at(base + 3 * window_bytes);

        Func bytes("two_bit_pack_bytes");
        bytes(byte_idx, blk) = cast<uint8_t>(b0 | (b1 << 2) | (b2 << 4) | (b3 << 6));
        return {{bytes}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, block_size/4)
        Var kk("kk"), blk("blk");
        int half_block = block_size_ / 2;
        int window_bytes = block_size_ / 8;

        Expr half = kk / half_block;
        Expr local = kk % half_block;
        Expr l = local / window_bytes;
        Expr m = local % window_bytes;
        Expr byte_idx = half * window_bytes + m;
        Expr q = (cast<uint32_t>(bytes(byte_idx, blk)) >> (l * 2)) & 3u;

        Func codes("two_bit_pack_codes");
        codes(kk, blk) = cast<int8_t>(cast<int32_t>(q) - qmax_);
        return {{codes}, {}};
    }

private:
    int block_size_, qmax_;
};

// encode(bit(kk, blk) in {0, 1}) -> window_size bytes; decode reverses it.
// Byte `kk % window_size` holds bit `kk / window_size` -- i.e. `num_windows`
// (= block_size/window_size) independent "planes" each contribute one bit
// per byte, the same "rotating bit position" scheme GGML's K-quants use for
// an out-of-band extra high bit per element (Q3_K's hmask, Q5_K's qh) --
// verified by hand this is a uniform way to write both formats' addressing
// (which their own source expresses via a `half`/`iter`-based case split
// instead). Meant to be combined with a lower-bit code_pack via
// CombinedBitsCode, not used standalone -- unlike FiveBitPack (which OR-
// accumulates its single high bit into one shared 32-bit word spanning the
// whole block), this spreads `num_windows` independent bits across
// `window_size` separate bytes instead.
class RotatingBitPack : public Halide::Approximation {
public:
    // `num_windows` is only needed for encode()'s OR-accumulation range
    // (decode()'s formulas don't need it -- bounds propagate backward from
    // the caller, like everywhere else in this file).
    RotatingBitPack(int window_size, int num_windows)
        : window_size_(window_size), num_windows_(num_windows) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func bit = inputs[0];  // bit(kk, blk), kk in [0, num_windows*window_size)
        Var byte_idx("byte_idx"), blk("blk");

        RDom rwin(0, num_windows_, "rwin");
        Func accum("rotating_bit_pack_accum");
        accum(byte_idx, blk) = cast<uint8_t>(0);
        accum(byte_idx, blk) = accum(byte_idx, blk) |
                               cast<uint8_t>(cast<uint32_t>(bit(byte_idx + rwin * window_size_, blk)) << rwin);
        return {{accum}, {accum}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk), byte_idx in [0, window_size)
        Var kk("kk"), blk("blk");
        Expr byte_idx = kk % window_size_;
        Expr bit_pos = kk / window_size_;
        Expr bit = (cast<uint32_t>(bytes(byte_idx, blk)) >> bit_pos) & 1u;
        Func out("rotating_bit_pack_bit");
        out(kk, blk) = cast<int8_t>(bit);
        return {{out}, {}};
    }

private:
    int window_size_, num_windows_;
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
// 4. Extern-delegated quantize, from-scratch lookup-table dequantize.
// ---------------------------------------------------------------------------

// The lookup-table analogue of SymmetricAffineQuantize/AffineQuantize, for
// GGML's codebook-quantized formats (IQ4_NL, MXFP4, ...): encode() doesn't
// compute anything itself -- it delegates entirely to a named GGML extern
// quantizer (matching ggml_extern_quantize.cpp's existing *_quantize_via_ggml
// symbols), since these formats' real quantizers run a per-block
// nearest-codeword search (IQ4_NL) or a transcendental/rounding-sensitive
// scale derivation (MXFP4) that isn't guaranteed bit-reproducible from a
// from-scratch Halide implementation -- see that file's header comment.
// Because of that, this encode() produces the *whole* packed byte buffer
// directly, not separate {codes, scale} Funcs the way every affine/symmetric
// quantize does.
//
// decode() reverses the packing itself (calling StructPack/NibblePack-or-
// BytePack/`scale_pack`'s decode() directly, exactly the way Compose/Apply
// call their own stages' encode()/decode() internally -- this class's
// decode() *is* a small composition, not application code that should be
// going through approximate_by() instead) to recover {codes, scale}, then
// looks codes up in a fixed codebook and multiplies by scale, then reshapes
// the result back to a flat row itself (see decode()'s comment -- unlike
// every affine/symmetric format, this class's encode() needs the *flat*
// input directly, so BlockReshape can't be composed on top the usual way)
// -- this direction has no search or rounding sensitivity, so it's implemented
// natively and is bit-/tolerance-exact like everything else here.
class LookupTableQuantize : public Halide::Approximation {
public:
    // Whether the scale field or the codes field comes first in on-disk
    // byte order -- {fp16 d; qs[16];} for IQ4_NL/MXFP4 (ScaleFirst) vs
    // {qs[64]; fp16 d;} for TQ2_0 (ScaleLast).
    enum FieldOrder { ScaleFirst,
                      ScaleLast };

    // `code_pack` decodes the codes field (NibblePack for IQ4_NL/MXFP4's
    // 4-bit codes, TwoBitPack for TQ2_0's 2-bit codes, TritPack for TQ1_0's
    // base-3 digits, ...); `code_bytes` is its packed width. `scale_pack`
    // decodes the scale field (Fp16Pack for IQ4_NL, E8M0Pack for MXFP4,
    // UE4M3Pack for NVFP4); `scale_bytes` is its packed width. `num_scales`
    // is how many independent scale values the block actually has (1 for
    // every format except NVFP4, whose 4 sub-blocks each carry their own
    // scale byte) -- when >1, `scale_pack` must decode to a
    // `scale(sub, blk)` Func (sub in [0, num_scales)) instead of the usual
    // `scale(blk)`, and dequantize indexes it by `kk`'s sub-block instead of
    // just `blk`. `table` is the codebook codes index into -- callers are
    // expected to pass a `static const` Buffer over `static const` backing
    // data (matching every existing per-format lookup_*() helper's own
    // idiom, e.g. iq4_nl_generators.cpp's kvalues_iq4nl), so the data has
    // static storage duration and this class can copy the lightweight
    // Buffer handle around freely.
    LookupTableQuantize(std::string extern_name, int block_size, Halide::Buffer<int8_t> table,
                        std::unique_ptr<Halide::Approximation> code_pack, int code_bytes,
                        std::unique_ptr<Halide::Approximation> scale_pack, int scale_bytes,
                        int num_scales, FieldOrder field_order)
        : extern_name_(std::move(extern_name)), block_size_(block_size), table_(table),
          code_pack_(std::move(code_pack)), code_bytes_(code_bytes),
          scale_pack_(std::move(scale_pack)), scale_bytes_(scale_bytes),
          num_scales_(num_scales), field_order_(field_order) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("lookup_table_quantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern(extern_name_, args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        StructPack layout = field_order_ == ScaleFirst ? StructPack({scale_bytes_, code_bytes_}, {1, 0}) : StructPack({code_bytes_, scale_bytes_}, {0, 1});
        DecodeResult unpacked = layout.decode(encoded);  // -> {codes_bytes, scale_bytes}
        DecodeResult codes_result = code_pack_->decode({unpacked.decoded[0]});
        DecodeResult scale_result = scale_pack_->decode({unpacked.decoded[1]});
        Func codes = codes_result.decoded[0], scale = scale_result.decoded[0];

        Buffer<int8_t> table = table_;
        Var kk("kk"), blk("blk");
        Func dequantized("lookup_table_dequantized");
        if (num_scales_ == 1) {
            dequantized(kk, blk) = cast<float>(cast<int32_t>(table(codes(kk, blk)))) * scale(blk);
        } else {
            int sub_size = block_size_ / num_scales_;
            dequantized(kk, blk) = cast<float>(cast<int32_t>(table(codes(kk, blk)))) * scale(kk / sub_size, blk);
        }

        // Unlike SymmetricAffineQuantize/AffineQuantize's codec (block-
        // indexed on both sides, with BlockReshape<->flat composed on top
        // separately -- see make_symmetric_block_scheme()), encode() here is
        // a whole-row extern call (it needs the *flat*, un-reshaped input,
        // just like every other define_extern-based quantizer in
        // ggml_extern_quantize.cpp), so this class has to do its own
        // block<->flat reshape internally on the decode side to keep
        // encode()/decode() shape-symmetric -- composing BlockReshape on top
        // the usual way would reshape the input *before* it reaches
        // encode()'s extern call, breaking its 1-D x_buf contract.
        Var k("k");
        Func flat("lookup_table_dequantized_flat");
        flat(k) = dequantized(k % block_size_, k / block_size_);
        return {{flat}, {}};
    }

private:
    std::string extern_name_;
    int block_size_;
    Halide::Buffer<int8_t> table_;
    std::unique_ptr<Halide::Approximation> code_pack_;
    int code_bytes_;
    std::unique_ptr<Halide::Approximation> scale_pack_;
    int scale_bytes_;
    int num_scales_;
    FieldOrder field_order_;
};

// ---------------------------------------------------------------------------
// 5. K-quants: combined-bit codes and per-sub-block (scale, min) packing.
// ---------------------------------------------------------------------------

// encode(codes(kk, blk)) -> one combined byte region {low_bytes; high_bytes}
// or {high_bytes; low_bytes}, depending on `order`; decode reverses it.
// Reproduces GGML's K-quant convention of splitting a wider-than-4-bit code
// into a low_pack-packed low part and a high_pack-packed high part stored
// separately (e.g. Q3_K's 2 low bits (TwoBitPack) + 1 high bit
// (RotatingBitPack), or Q6_K's 4 low bits (SubBlockNibblePack) + 2 high bits
// (TwoBitPack)) -- `code = low + high*high_weight - offset`, verified by
// hand to collapse both formats' actual bit-OR reconstruction into one
// formula (OR and + agree whenever the low/high bit ranges don't overlap,
// which they don't here by construction: `high_weight` is always the low
// part's own value range).
class CombinedBitsCode : public Halide::Approximation {
public:
    enum Order { LowFirst,
                 HighFirst };

    CombinedBitsCode(std::unique_ptr<Halide::Approximation> low_pack, int low_bytes,
                     std::unique_ptr<Halide::Approximation> high_pack, int high_bytes,
                     int high_weight, int offset, Order order)
        : low_pack_(std::move(low_pack)), low_bytes_(low_bytes),
          high_pack_(std::move(high_pack)), high_bytes_(high_bytes),
          high_weight_(high_weight), offset_(offset), order_(order) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func codes = inputs[0];  // codes(kk, blk), the combined (pre-split) value
        Var kk("kk"), blk("blk");

        // `combined` is always >= 0 by construction (offset_ is exactly
        // what decode() subtracts after reconstructing low + high*weight),
        // so %/ below don't need to handle negative operands.
        Expr combined = cast<int32_t>(codes(kk, blk)) + offset_;
        Func low("combined_bits_code_low");
        low(kk, blk) = cast<int8_t>(combined % high_weight_);
        Func high("combined_bits_code_high");
        high(kk, blk) = cast<int8_t>(combined / high_weight_);

        EncodeResult low_r = low_pack_->encode({low});
        EncodeResult high_r = high_pack_->encode({high});

        std::vector<Func> handles = low_r.handles;
        handles.insert(handles.end(), high_r.handles.begin(), high_r.handles.end());

        StructPack layout = order_ == LowFirst ? StructPack({low_bytes_, high_bytes_}, {0, 1}) : StructPack({high_bytes_, low_bytes_}, {1, 0});
        EncodeResult packed = layout.encode({low_r.encoded[0], high_r.encoded[0]});
        return {packed.encoded, handles};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        StructPack layout = order_ == LowFirst ? StructPack({low_bytes_, high_bytes_}, {0, 1}) : StructPack({high_bytes_, low_bytes_}, {1, 0});
        DecodeResult unpacked = layout.decode(encoded);  // -> {low_bytes, high_bytes}
        Func low = low_pack_->decode({unpacked.decoded[0]}).decoded[0];
        Func high = high_pack_->decode({unpacked.decoded[1]}).decoded[0];

        Var kk("kk"), blk("blk");
        Func code("combined_bits_code");
        code(kk, blk) = cast<int8_t>((cast<int32_t>(low(kk, blk)) + high_weight_ * cast<int32_t>(high(kk, blk))) - offset_);
        return {{code}, {}};
    }

private:
    std::unique_ptr<Halide::Approximation> low_pack_;
    int low_bytes_;
    std::unique_ptr<Halide::Approximation> high_pack_;
    int high_bytes_;
    int high_weight_, offset_;
    Order order_;
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

// The K-quant analogue of SymmetricAffineQuantize/AffineQuantize's shape,
// but with a *two-level* scale hierarchy instead of one scale/block:
// {fp16 d; [fp16 dmin;] scale_min; codes;} where every sub-block (`kk /
// sub_size`) has its own (scale, [min]) pair from `scale_min_pack`, itself
// then multiplied by the super-block-wide `d`/`dmin`. encode() delegates to
// GGML's own reference the same way LookupTableQuantize does (every K-quant
// quantizer runs an iterative per-sub-block error-minimizing search --
// make_qkx2_quants/make_q3_quants/make_qx_quants in ggml-quants.c -- not
// guaranteed bit-reproducible from scratch). `field_widths`/`input_index`
// describe the on-disk field order exactly like StructPack -- the logical
// field indices are {d, dmin, scale_min, code} when `has_min` (Q2_K/Q4_K/
// Q5_K), or {d, scale, code} when not (Q3_K/Q6_K, whose `scale_min_pack`
// then decodes to a single scale Func, not a {scale, min} pair).
class KQuantDequantize : public Halide::Approximation {
public:
    KQuantDequantize(std::string extern_name, int block_size, int sub_size,
                     std::vector<int> field_widths, std::vector<int> input_index, bool has_min,
                     std::unique_ptr<Halide::Approximation> scale_min_pack,
                     std::unique_ptr<Halide::Approximation> code_pack)
        : extern_name_(std::move(extern_name)), block_size_(block_size), sub_size_(sub_size),
          field_widths_(std::move(field_widths)), input_index_(std::move(input_index)), has_min_(has_min),
          scale_min_pack_(std::move(scale_min_pack)), code_pack_(std::move(code_pack)) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("k_quant_dequantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern(extern_name_, args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        StructPack layout(field_widths_, input_index_);
        DecodeResult unpacked = layout.decode(encoded);

        Func d = Fp16Pack{}.decode({unpacked.decoded[0]}).decoded[0];
        int scale_min_idx = has_min_ ? 2 : 1;
        int code_idx = has_min_ ? 3 : 2;
        DecodeResult sm = scale_min_pack_->decode({unpacked.decoded[scale_min_idx]});
        Func scale = sm.decoded[0];
        Func codes = code_pack_->decode({unpacked.decoded[code_idx]}).decoded[0];

        Var kk("kk"), blk("blk");
        Func dequantized("k_quant_dequantized");
        if (has_min_) {
            Func dmin = Fp16Pack{}.decode({unpacked.decoded[1]}).decoded[0];
            Func min = sm.decoded[1];
            dequantized(kk, blk) = d(blk) * cast<float>(scale(kk / sub_size_, blk)) * cast<float>(codes(kk, blk)) -
                                   dmin(blk) * cast<float>(min(kk / sub_size_, blk));
        } else {
            dequantized(kk, blk) = d(blk) * cast<float>(scale(kk / sub_size_, blk)) * cast<float>(codes(kk, blk));
        }

        // Same flat<->block reshape rationale as LookupTableQuantize: this
        // class's encode() needs the flat input directly for its extern
        // call, so BlockReshape can't be composed on top the usual way.
        Var k("k");
        Func flat("k_quant_dequantized_flat");
        flat(k) = dequantized(k % block_size_, k / block_size_);
        return {{flat}, {}};
    }

private:
    std::string extern_name_;
    int block_size_, sub_size_;
    std::vector<int> field_widths_, input_index_;
    bool has_min_;
    std::unique_ptr<Halide::Approximation> scale_min_pack_;
    std::unique_ptr<Halide::Approximation> code_pack_;
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
// sub-formula across formats the way SubBlockNibblePack/TwoBitPack/
// RotatingBitPack turned out to be for the K-quants. Rather than force an
// artificial shared abstraction over 3 genuinely different bit layouts, each
// format below is its own small, direct Approximation class: encode() is
// the same extern-delegation shape as LookupTableQuantize/KQuantDequantize
// (GGML's own reference quantizer for these runs a per-block codebook
// search -- see ggml_extern_quantize.cpp), and decode() is a mechanical,
// verified-unchanged transcription of iq2_s_generators.cpp's/
// iq3_xxs_generators.cpp's/iq3_s_generators.cpp's own (already bit-exact)
// dequantize math, just reading from a `bytes(byte, blk)` Func instead of
// an `Input<Buffer<uint8_t, 2>>` directly.

// IQ2_S: 256-element superblock, 8 groups of 32 elements, grid index = an 8-
// bit qs byte plus 2 extra high bits from a per-group qh byte (1024-entry,
// 64-bit iq2s_grid, 8 output bytes/index); signs stored directly (no
// ksigns_iq2xs indirection); scale a nibble byte array (2 groups/byte) via
// `d*(0.5+nibble)*0.25` -- {fp16 d; qs[32]; signs[32]; qh[8]; scales[8];},
// 82 bytes.
class IQ2SGridDequantize : public Halide::Approximation {
public:
    IQ2SGridDequantize()
        : grid_(1024, "iq2s_grid") {
        for (int idx = 0; idx < 1024; idx++) {
            grid_(idx) = iq_grids::iq2s_grid[idx];
        }
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("iq2s_grid_dequantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern("iq2_s_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 82)
        Var kk("kk"), blk("blk");

        constexpr int kQsOffset = 2;
        constexpr int kSignsOffset = kQsOffset + 256 / 8;    // 34
        constexpr int kQhOffset = kSignsOffset + 256 / 8;    // 66
        constexpr int kScalesOffset = kQhOffset + 256 / 32;  // 74

        Expr ib32 = kk / 32;
        Expr local = kk % 32;
        Expr l = local / 8;  // 0..3
        Expr j = local % 8;  // 0..7

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

        Expr d_lo = cast<uint16_t>(bytes(0, blk));
        Expr d_hi = cast<uint16_t>(bytes(1, blk));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr db = d * (0.5f + cast<float>(nibble)) * 0.25f;

        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j)) != 0;

        Func dequantized("iq2s_grid_dequantized");
        dequantized(kk, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        // Same flat<->block reshape rationale as LookupTableQuantize/
        // KQuantDequantize: encode() needs the flat input directly for its
        // extern call, so BlockReshape can't be composed on top the usual
        // way.
        Var k("k");
        Func flat("iq2s_grid_dequantized_flat");
        flat(k) = dequantized(k % 256, k / 256);
        return {{flat}, {}};
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
        : grid_(256, "iq3xxs_grid"), ksigns_(128, "ksigns_iq2xs") {
        for (int idx = 0; idx < 256; idx++) {
            grid_(idx) = iq_grids::iq3xxs_grid[idx];
        }
        for (int idx = 0; idx < 128; idx++) {
            ksigns_(idx) = iq_grids::ksigns_iq2xs[idx];
        }
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("iq3xxs_grid_dequantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern("iq3_xxs_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 98)
        Var kk("kk"), blk("blk");

        constexpr int kQsOffset = 2;
        constexpr int kScalesSignsOffset = kQsOffset + 256 / 4;  // 66

        Expr ib32 = kk / 32;
        Expr local = kk % 32;
        Expr l = local / 8;   // 0..3
        Expr j8 = local % 8;  // 0..7
        Expr j4 = j8 % 4;     // byte within the 4-byte grid entry
        Expr half = j8 / 4;   // 0 (grid1) or 1 (grid2)

        Expr grid_qs_idx = ib32 * 8 + l * 2 + half;
        Expr grid_idx = bytes(kQsOffset + grid_qs_idx, blk);

        Expr ss_base = kScalesSignsOffset + ib32 * 4;
        Expr b0 = cast<uint32_t>(bytes(ss_base + 0, blk));
        Expr b1 = cast<uint32_t>(bytes(ss_base + 1, blk));
        Expr b2 = cast<uint32_t>(bytes(ss_base + 2, blk));
        Expr b3 = cast<uint32_t>(bytes(ss_base + 3, blk));
        Expr aux32 = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);

        Expr d_lo = cast<uint16_t>(bytes(0, blk));
        Expr d_hi = cast<uint16_t>(bytes(1, blk));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr db = d * (0.5f + cast<float>(aux32 >> 28)) * 0.5f;

        Expr sign_idx = cast<uint8_t>((aux32 >> (cast<uint32_t>(l) * 7)) & 127);
        Buffer<uint8_t> ksigns = ksigns_;
        Expr signs = ksigns(sign_idx);

        Buffer<uint32_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j8)) != 0;

        Func dequantized("iq3xxs_grid_dequantized");
        dequantized(kk, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        Var k("k");
        Func flat("iq3xxs_grid_dequantized_flat");
        flat(k) = dequantized(k % 256, k / 256);
        return {{flat}, {}};
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
        : grid_(512, "iq3s_grid") {
        for (int idx = 0; idx < 512; idx++) {
            grid_(idx) = iq_grids::iq3s_grid[idx];
        }
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("iq3s_grid_dequantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern("iq3_s_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 110)
        Var kk("kk"), blk("blk");

        constexpr int kQsOffset = 2;
        constexpr int kQhOffset = kQsOffset + 256 / 4;         // 66
        constexpr int kSignsOffset = kQhOffset + 256 / 32;     // 74
        constexpr int kScalesOffset = kSignsOffset + 256 / 8;  // 106

        Expr grp = kk / 32;  // 0..7
        Expr local = kk % 32;
        Expr l = local / 8;   // 0..3
        Expr j8 = local % 8;  // 0..7
        Expr j4 = j8 % 4;     // byte within the 4-byte grid entry
        Expr half = j8 / 4;   // 0 (first grid index of this l) or 1 (second)

        Expr qs_byte = bytes(kQsOffset + grp * 8 + l * 2 + half, blk);
        Expr qh_byte = cast<uint32_t>(bytes(kQhOffset + grp, blk));
        Expr bit_pos = l * 2 + half;
        Expr high_bit = (qh_byte >> cast<uint32_t>(bit_pos)) & 1;
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high_bit << 8))), 0, 511);

        Expr signs_byte = bytes(kSignsOffset + grp * 4 + l, blk);
        Expr sign_bit = (cast<uint32_t>(signs_byte) & (cast<uint32_t>(1) << j8)) != 0;

        Expr scales_byte = bytes(kScalesOffset + grp / 2, blk);
        Expr nibble = select((grp % 2) == 0, scales_byte & 0x0f, scales_byte >> 4);

        Expr d_lo = cast<uint16_t>(bytes(0, blk));
        Expr d_hi = cast<uint16_t>(bytes(1, blk));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr db = d * (1.0f + 2.0f * cast<float>(nibble));

        Buffer<uint32_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint32_t>(j4) * 8)) & 0xff);

        Func dequantized("iq3s_grid_dequantized");
        dequantized(kk, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);

        Var k("k");
        Func flat("iq3s_grid_dequantized_flat");
        flat(k) = dequantized(k % 256, k / 256);
        return {{flat}, {}};
    }

private:
    Halide::Buffer<uint32_t> grid_;
};

// IQ4_XS: 256-element superblock, 8 sub-blocks of 32 elements, the
// superblock generalization of IQ4_NL's fixed 16-value codebook -- plain
// 4-bit codes (SubBlockNibblePack-shaped, but written directly here since
// there's no separate reusable pack class for this exact "16 bytes per
// sub-block, low half then high half" addressing) into the same
// kvalues_iq4nl table IQ4_NL uses, scaled by `d * (ls - 32)` where `ls` is
// a 6-bit per-sub-block value split 4-bits-low (scales_l, 2 sub-blocks/
// byte) + 2-bits-high (scales_h, a little-endian uint16, 2 bits/sub-block)
// -- a two-level scale like the K-quants' `d*scale(sub,blk)` (no min), not
// LookupTableQuantize's flat one-scale-per-block shape, so this needs its
// own small class rather than reusing either existing top-level class --
// {fp16 d; scales_h[2]; scales_l[4]; qs[128];}, 136 bytes.
class IQ4XSDequantize : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];
        Func blocks("iq4xs_dequantize_blocks");
        std::vector<ExternFuncArgument> args = {flat};
        blocks.define_extern("iq4_xs_quantize_via_ggml", args, UInt(8), 2, NameMangling::C);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte, blk), byte in [0, 136)
        Var kk("kk"), blk("blk");

        constexpr int kScalesHOffset = 2;
        constexpr int kScalesLOffset = 4;
        constexpr int kQsOffset = 8;

        Expr ib = kk / 32;  // sub-block, 0..7
        Expr local = kk % 32;
        Expr j = local % 16;  // 0..15
        Expr is_low = local < 16;

        Expr qs_byte_idx = ib * 16 + j;
        Expr byte = bytes(kQsOffset + qs_byte_idx, blk);
        Expr nibble = cast<int32_t>(select(is_low, byte & 0x0f, byte >> 4));

        static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                           1, 13, 25, 38, 53, 69, 89, 113};
        static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl_xs");
        Expr val = cast<int32_t>(table(nibble));

        // ls = (scales_l[ib/2] >> 4*(ib%2)) & 0xf | ((scales_h >> 2*ib) & 3) << 4
        Expr scales_l_byte = bytes(kScalesLOffset + ib / 2, blk);
        Expr low4 = cast<int32_t>((scales_l_byte >> ((ib % 2) * 4)) & 0x0f);

        Expr sh_lo = cast<uint16_t>(bytes(kScalesHOffset + 0, blk));
        Expr sh_hi = cast<uint16_t>(bytes(kScalesHOffset + 1, blk));
        Expr scales_h = sh_lo | (sh_hi << 8);
        Expr high2 = cast<int32_t>((scales_h >> cast<uint16_t>(ib * 2)) & 3);

        Expr ls = low4 | (high2 << 4);

        Expr d_lo = cast<uint16_t>(bytes(0, blk));
        Expr d_hi = cast<uint16_t>(bytes(1, blk));
        Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
        Expr dl = d * cast<float>(ls - 32);

        Func dequantized("iq4xs_dequantized");
        dequantized(kk, blk) = dl * cast<float>(val);

        Var k("k");
        Func flat("iq4xs_dequantized_flat");
        flat(k) = dequantized(k % 256, k / 256);
        return {{flat}, {}};
    }
};

// Compose now owns every stage it's given (see Approximation.h), and its
// N-ary constructor flattens what used to be nested Compose(outer,
// Compose(..., Compose(...))) chains into one ordered list -- stage 0 is
// outermost (its encoded output is the whole thing's result), the last
// stage is innermost (closest to the original values). Each "make_*"
// function below just returns that flat Compose directly.
//
// quantize -> pack codes -> pack scale -> concatenate into one byte buffer
// with the scale stored first (matching every GGML block_* struct's
// `{fp16 d; ...qs;}` layout) -- everything a "block_qN_0-style" scheme needs
// *except* the flat<->block reshape, because this piece operates directly on
// already block-indexed (kk, blk) values. This is the part vec_dot reuses
// as-is (its Inputs are already block-indexed packed buffers, so there's
// nothing to reshape); make_symmetric_block_scheme() below adds BlockReshape
// on top of this for quantize_row/dequantize_row's flat 1-D array interface.
inline Halide::Compose make_symmetric_block_codec(
    int block_size, int qmax, RoundingMode rounding, ScaleAnchor anchor, int code_bits) {
    using namespace Halide;
    // NibblePack vs BytePack vs BitPack is a runtime choice, so it can't be
    // a plain value argument below like everything else -- Compose/Apply's
    // constructors also accept an already-type-erased
    // std::unique_ptr<Approximation> for exactly this case.
    std::unique_ptr<Approximation> code_pack =
        code_bits == 4 ? std::unique_ptr<Approximation>(std::make_unique<NibblePack>(block_size, qmax)) : code_bits == 1 ? std::unique_ptr<Approximation>(std::make_unique<BitPack>()) :
                                                                                                                           std::unique_ptr<Approximation>(std::make_unique<BytePack>());
    int code_bytes = code_bits == 4 ? block_size / 2 : code_bits == 1 ? block_size / 8 :
                                                                        block_size;
    return Compose{
        StructPack{{2, code_bytes}, {1, 0}},
        Apply{1, 1, 1, Fp16Pack{}},
        Apply{0, 1, 1, std::move(code_pack)},
        SymmetricAffineQuantize{block_size, qmax, rounding, anchor},
    };
}

// make_symmetric_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline Halide::Compose make_symmetric_block_scheme(
    int block_size, int qmax, RoundingMode rounding, ScaleAnchor anchor, int code_bits) {
    return Halide::Compose{
        make_symmetric_block_codec(block_size, qmax, rounding, anchor, code_bits),
        BlockReshape{block_size},
    };
}

// quantize -> pack codes -> pack scale -> pack min -> concatenate, scale
// then min then codes (matching block_q4_1/block_q5_1's `{fp16 d; fp16 m;
// ...qs;}` layout) -- the affine (min+scale) counterpart to
// make_symmetric_block_codec(), used by Q4_1 (code_bits=4, ClampedInt8). Q5_1
// pairs with make_affine_5bit_block_codec() below instead, since it also
// needs the qh high-bit field.
inline Halide::Compose make_affine_block_codec(
    int block_size, int levels, AffineRounding rounding, int code_bits) {
    using namespace Halide;
    std::unique_ptr<Approximation> code_pack =
        code_bits == 4 ? std::unique_ptr<Approximation>(std::make_unique<NibblePack>(block_size, /*qmax=*/0)) : std::unique_ptr<Approximation>(std::make_unique<BytePack>());
    return Compose{
        StructPack{{2, 2, code_bits == 4 ? block_size / 2 : block_size}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},            // pack min
        Apply{1, 1, 1, Fp16Pack{}},            // pack scale
        Apply{0, 1, 1, std::move(code_pack)},  // pack codes
        AffineQuantize{block_size, levels, rounding},
    };
}

// make_affine_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline Halide::Compose make_affine_block_scheme(
    int block_size, int levels, AffineRounding rounding, int code_bits) {
    return Halide::Compose{
        make_affine_block_codec(block_size, levels, rounding, code_bits),
        BlockReshape{block_size},
    };
}

// Symmetric quantize (like make_symmetric_block_codec()) but 5-bit: the
// extra 5th bit goes through FiveBitPack instead of NibblePack, producing
// an extra `qh` field between the scale and the nibble bytes in the output
// layout (matching block_q5_0's `{fp16 d; qh[4]; qs[16];}`). `qmax` is
// always 16 (5-bit signed range [-16, 15]).
inline Halide::Compose make_symmetric_5bit_block_codec(int block_size, int qmax) {
    using namespace Halide;
    return Compose{
        StructPack{{2, 4, block_size / 2}, {2, 1, 0}},
        Apply{2, 1, 1, Fp16Pack{}},
        Apply{0, 1, 2, FiveBitPack{block_size, qmax}},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::TruncateHalfUpWithOffset, ScaleAnchor::ExtremeSignedValue},
    };
}

// make_symmetric_5bit_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline Halide::Compose make_symmetric_5bit_block_scheme(int block_size, int qmax) {
    return Halide::Compose{
        make_symmetric_5bit_block_codec(block_size, qmax),
        BlockReshape{block_size},
    };
}

// Affine quantize (like make_affine_block_codec()) but 5-bit: same
// FiveBitPack addition as make_symmetric_5bit_block_codec(), plus the min
// field affine quantization needs, matching block_q5_1's
// `{fp16 d; fp16 m; qh[4]; qs[16];}`. `qmax=0` when calling FiveBitPack
// here (unlike Q5_0's 16): AffineQuantize's codes are already unsigned
// [0, levels], not centered, so there's no offset to re-apply before
// splitting into nibble+high-bit.
inline Halide::Compose make_affine_5bit_block_codec(int block_size, int levels, AffineRounding rounding) {
    using namespace Halide;
    return Compose{
        StructPack{{2, 2, 4, block_size / 2}, {2, 3, 1, 0}},
        Apply{3, 1, 1, Fp16Pack{}},                           // pack min
        Apply{2, 1, 1, Fp16Pack{}},                           // pack scale
        Apply{0, 1, 2, FiveBitPack{block_size, /*qmax=*/0}},  // pack codes
        AffineQuantize{block_size, levels, rounding},
    };
}

// make_affine_5bit_block_codec() + BlockReshape: the full flat-array
// quantize_row/dequantize_row scheme.
inline Halide::Compose make_affine_5bit_block_scheme(int block_size, int levels, AffineRounding rounding) {
    return Halide::Compose{
        make_affine_5bit_block_codec(block_size, levels, rounding),
        BlockReshape{block_size},
    };
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
inline Halide::Compose make_symmetric_byte_sum_block_codec(int block_size, int qmax) {
    using namespace Halide;
    return Compose{
        StructPack{{2, 2, block_size}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},  // pack sum
        Apply{1, 1, 1, Fp16Pack{}},  // pack scale
        Apply{0, 1, 1, BytePack{}},  // pack codes
        AppendCodeSum{block_size},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::Nearest, ScaleAnchor::AbsMax},
    };
}

// make_symmetric_byte_sum_block_codec() + BlockReshape: the full flat-array
// quantize_row scheme (see above -- there is normally no matching
// dequantize_row Generator, but this is still available for completeness/
// consistency with every other scheme here).
inline Halide::Compose make_symmetric_byte_sum_block_scheme(int block_size, int qmax) {
    return Halide::Compose{
        make_symmetric_byte_sum_block_codec(block_size, qmax),
        BlockReshape{block_size},
    };
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
inline Halide::Compose make_q8_k_codec(int block_size, int qmax) {
    using namespace Halide;
    return Compose{
        StructPack{{4, block_size, (block_size / 16) * 2}, {1, 0, 2}},
        Apply{2, 1, 1, Int16Pack{}},  // pack bsums
        Apply{1, 1, 1, F32Pack{}},    // pack scale
        Apply{0, 1, 1, BytePack{}},   // pack codes
        AppendGroupSumsInt16{16},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::NearestEvenClampedHigh,
                                ScaleAnchor::ExtremeSignedValueTwoStep},
    };
}

inline Halide::Compose make_q8_k_scheme(int block_size, int qmax) {
    return Halide::Compose{
        make_q8_k_codec(block_size, qmax),
        BlockReshape{block_size},
    };
}

// IQ4_NL: 32-element blocks, 4-bit codes into a 16-value non-uniform
// codebook, one fp16 scale per block -- {fp16 d; qs[16];}, 18 bytes.
// Unlike every affine/symmetric scheme above, LookupTableQuantize already
// handles the flat<->block reshape internally (its encode() needs the whole
// flat row for its extern call -- see its class comment), so there's no
// separate "_codec" (block-indexed) form to compose BlockReshape onto here.
inline Halide::Compose make_iq4_nl_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl");
    return Compose{
        LookupTableQuantize{"iq4_nl_quantize_via_ggml", 32, table,
                            std::make_unique<NibblePack>(32, 0), 16,
                            std::make_unique<Fp16Pack>(), 2, 1,
                            LookupTableQuantize::ScaleFirst},
    };
}

// MXFP4: 32-element blocks, 4-bit codes into the same-shaped 16-value
// codebook as IQ4_NL (different values), one E8M0 (1-byte, power-of-two)
// scale per block -- {e8m0 e; qs[16];}, 17 bytes.
inline Halide::Compose make_mxfp4_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_mxfp4");
    return Compose{
        LookupTableQuantize{"mxfp4_quantize_via_ggml", 32, table,
                            std::make_unique<NibblePack>(32, 0), 16,
                            std::make_unique<E8M0Pack>(), 1, 1,
                            LookupTableQuantize::ScaleFirst},
    };
}

// TQ2_0: 256-element superblock, 2-bit codes (each in {0,1,2}, meaning
// {-1,0,1}) via TwoBitPack's window-interleaved layout, one fp16 scale --
// {qs[64]; fp16 d;}, 66 bytes -- qs *before* d, unlike every other format
// here (LookupTableQuantize::ScaleLast).
inline Halide::Compose make_tq2_0_scheme() {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 4, "kvalues_tq2_0");
    return Compose{
        LookupTableQuantize{"tq2_0_quantize_via_ggml", 256, table,
                            std::make_unique<TwoBitPack>(256, 0), 64,
                            std::make_unique<Fp16Pack>(), 2, 1,
                            LookupTableQuantize::ScaleLast},
    };
}

// TQ1_0: 256-element superblock, base-3 codes (each in {0,1,2}, meaning
// {-1,0,1}) via TritPack's 5-trits/byte (+4-trits/byte tail) packing, one
// fp16 scale -- {qs[48]; qh[4]; fp16 d;}, 54 bytes -- qs+qh (combined, 52
// bytes) *before* d, like TQ2_0 (LookupTableQuantize::ScaleLast). Reuses
// TQ2_0's exact {-1, 0, 1, unused} codebook (TritPack's codes are the same
// raw 0/1/2 digit either way).
inline Halide::Compose make_tq1_0_scheme() {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 4, "kvalues_tq1_0");
    return Compose{
        LookupTableQuantize{"tq1_0_quantize_via_ggml", 256, table,
                            std::make_unique<TritPack>(), 52,
                            std::make_unique<Fp16Pack>(), 2, 1,
                            LookupTableQuantize::ScaleLast},
    };
}

// NVFP4: 64-element block, 4 sub-blocks of 16 elements each, 4-bit codes
// into the same 16-value codebook MXFP4 uses (NVFP4 is MXFP4 with
// finer-grained scales), one UE4M3 scale *per sub-block* via UE4M3Pack --
// {d[4]; qs[32];}, 36 bytes -- LookupTableQuantize's num_scales=4 (not 1)
// is what makes each sub-block's dequantize use its own scale byte instead
// of one shared scale for the whole 64-element block.
inline Halide::Compose make_nvfp4_scheme() {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table(const_cast<int8_t *>(kValues), 16, "kvalues_nvfp4");
    return Compose{
        LookupTableQuantize{"nvfp4_quantize_via_ggml", 64, table,
                            std::make_unique<SubBlockNibblePack>(16, 4, 0), 32,
                            std::make_unique<UE4M3Pack>(), 4, 4,
                            LookupTableQuantize::ScaleFirst},
    };
}

// Q4_K: 256-element superblock, 8 sub-blocks of 32 elements each, plain
// 4-bit codes (SubBlockNibblePack) and get_scale_min_k4-packed (scale, min)
// pairs (K4ScaleMinPack) -- {fp16 d; fp16 dmin; scales[12]; qs[128];}, 144
// bytes, fields already in {d, dmin, scale_min, code} logical order.
inline Halide::Compose make_q4_k_scheme() {
    using namespace Halide;
    return Compose{
        KQuantDequantize{"q4_k_quantize_via_ggml", 256, 32, {2, 2, 12, 128}, {0, 1, 2, 3}, true, std::make_unique<K4ScaleMinPack>(), std::make_unique<SubBlockNibblePack>(64, 4, 0)},
    };
}

// Q5_K: same super-block/sub-block/scale-min shape as Q4_K, but each code
// is 5 bits: a plain 4-bit low nibble (SubBlockNibblePack) plus a 5th high
// bit from a separate 32-byte, 8-window rotating-bit array (RotatingBitPack)
// -- {fp16 d; fp16 dmin; scales[12]; qh[32]; qs[128];}, 176 bytes. qh+qs are
// adjacent in memory, so they're treated as one 160-byte combined "code"
// field here, with CombinedBitsCode doing its own internal qh/qs split
// (HighFirst: qh before qs) -- offset=0 since Q5_K's code is a plain
// 0..31 unsigned magnitude, not recentered like Q3_K's.
inline Halide::Compose make_q5_k_scheme() {
    using namespace Halide;
    return Compose{
        KQuantDequantize{"q5_k_quantize_via_ggml", 256, 32, {2, 2, 12, 160}, {0, 1, 2, 3}, true, std::make_unique<K4ScaleMinPack>(), std::make_unique<CombinedBitsCode>(std::make_unique<SubBlockNibblePack>(64, 4, 0), 128, std::make_unique<RotatingBitPack>(32, 8), 32, 16, 0, CombinedBitsCode::HighFirst)},
    };
}

// Q2_K: 256-element superblock, 16 sub-blocks of 16 elements each, plain
// 2-bit codes (TwoBitPack) and independent per-sub-block nibble-pair
// (scale, min) (NibblePairPack, no bit-interleaving) -- {scales[16]; qs[64];
// fp16 d; fp16 dmin;}, 84 bytes, fields on-disk in {scale_min, code, d,
// dmin} order (scale_min/code *before* d/dmin, unlike Q4_K/Q5_K).
inline Halide::Compose make_q2_k_scheme() {
    using namespace Halide;
    return Compose{
        KQuantDequantize{"q2_k_quantize_via_ggml", 256, 16, {16, 64, 2, 2}, {2, 3, 0, 1}, true, std::make_unique<NibblePairPack>(), std::make_unique<TwoBitPack>(256, 0)},
    };
}

// Q3_K: 256-element superblock, 16 sub-blocks of 16 elements each, no min
// (symmetric, not affine) -- each code is 3 bits: 2 low bits (TwoBitPack)
// plus a high bit from a 32-byte, 8-window rotating-bit "hmask" array
// (RotatingBitPack), recentered by -4 (offset=4, matching a signed
// [-4, 3] range); scale is 16 SIGNED 6-bit values, its own bit-interleaving
// distinct from get_scale_min_k4 (Q3KScalePack) -- {hmask[32]; qs[64];
// scales[12]; fp16 d;}, 110 bytes. hmask+qs are adjacent in memory, treated
// as one 96-byte combined "code" field (HighFirst: hmask before qs); fields
// on-disk in {code, scale, d} order (has_min=false logical order is
// {d, scale, code}).
inline Halide::Compose make_q3_k_scheme() {
    using namespace Halide;
    return Compose{
        KQuantDequantize{"q3_k_quantize_via_ggml", 256, 16, {96, 12, 2}, {2, 1, 0}, false, std::make_unique<Q3KScalePack>(), std::make_unique<CombinedBitsCode>(std::make_unique<TwoBitPack>(256, 0), 64, std::make_unique<RotatingBitPack>(32, 8), 32, 4, 4, CombinedBitsCode::HighFirst)},
    };
}

// Q6_K: 256-element superblock, 16 sub-blocks of 16 elements each, no min --
// each code is 6 bits: a plain 4-bit low nibble over *two* 128-element
// halves (SubBlockNibblePack) plus 2 high bits (TwoBitPack), recentered by
// -32; scale is 16 plain SIGNED int8 values, no bit-interleaving at all
// (reuses BytePack directly -- its plain reinterpret<int8_t> is exactly
// what this needs) -- {ql[128]; qh[64]; scales[16]; fp16 d;}, 210 bytes.
// ql+qh are adjacent in memory, treated as one 192-byte combined "code"
// field (LowFirst: ql before qh).
inline Halide::Compose make_q6_k_scheme() {
    using namespace Halide;
    return Compose{
        KQuantDequantize{"q6_k_quantize_via_ggml", 256, 16, {192, 16, 2}, {2, 1, 0}, false, std::make_unique<BytePack>(), std::make_unique<CombinedBitsCode>(std::make_unique<SubBlockNibblePack>(128, 2, 0), 128, std::make_unique<TwoBitPack>(256, 0), 64, 16, 32, CombinedBitsCode::LowFirst)},
    };
}

// IQ2_S/IQ3_XXS/IQ3_S: see IQ2SGridDequantize/IQ3XXSGridDequantize/
// IQ3SGridDequantize above for the bit-layout rationale -- each is already
// a complete, self-contained Approximation (extern-delegated encode(),
// bespoke grid+sign+scale decode(), its own flat<->block reshape), so these
// factories just wrap them in a single-stage Compose for consistency with
// every other make_*_scheme() here.
inline Halide::Compose make_iq2_s_scheme() {
    return Halide::Compose{IQ2SGridDequantize{}};
}

inline Halide::Compose make_iq3_xxs_scheme() {
    return Halide::Compose{IQ3XXSGridDequantize{}};
}

inline Halide::Compose make_iq3_s_scheme() {
    return Halide::Compose{IQ3SGridDequantize{}};
}

inline Halide::Compose make_iq4_xs_scheme() {
    return Halide::Compose{IQ4XSDequantize{}};
}

}  // namespace ggml_halide
