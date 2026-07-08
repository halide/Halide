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
//
// `block_indexed` selects what the "flat" side looks like:
//   - false (default): a fully-flat 1-D row f(k), k = blk*block_size + within
//     -- the shape quantize_row/dequantize_row want.
//   - true: a block-indexed 2-D f(kk, blk), within-block index kept separate
//     from the block index -- the shape a per-block vec_dot reduction wants
//     (so the block index stays a distinct RVar for the SDOT rfactor hoist).
// In block-indexed mode a single-extent reshape is a (kk,blk) passthrough,
// and a multi-extent one collapses the nested dims (elem,l,group,blk) into
// (kk,blk) -- the only difference from flat mode is folding blk into k or not.
class BlockReshape : public Halide::Approximation {
public:
    explicit BlockReshape(int block_size, bool block_indexed = false)
        : extents_{block_size}, block_indexed_(block_indexed) {
    }
    explicit BlockReshape(std::vector<int> extents, bool block_indexed = false)
        : extents_(std::move(extents)), block_indexed_(block_indexed) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func flat = inputs[0];  // f(k), or f(kk, blk) when block_indexed_
        std::vector<Var> dims = block_vars();
        Var blk("blk");

        // packed(d0, d1, ..., blk) = flat(<within>), within = d0 + e0*d1 + ...
        Expr within = cast<int>(0);
        int stride = 1;
        for (size_t i = 0; i < dims.size(); i++) {
            within += dims[i] * stride;
            stride *= extents_[i];
        }
        std::vector<Var> args = dims;
        args.push_back(blk);
        Func packed("block_reshape_packed");
        packed(args) = block_indexed_ ? flat(within, blk) : flat(blk * block_size() + within);
        return {{packed}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func packed = encoded[0];
        Var k("k"), kk("kk"), blk("blk");

        // Read packed(within%e0, (within/e0)%e1, ..., block); the within-block
        // index and block index come from either a flat k or a (kk, blk) pair.
        Expr within = block_indexed_ ? (Expr)kk : k % block_size();
        Expr block = block_indexed_ ? (Expr)blk : k / block_size();
        std::vector<Expr> args;
        Expr rem = within;
        for (int e : extents_) {
            args.push_back(rem % e);
            rem = rem / e;
        }
        args.push_back(block);
        Func out("block_reshape_unpacked");
        if (block_indexed_) {
            out(kk, blk) = packed(args);
        } else {
            out(k) = packed(args);
        }
        return {{out}, {}};
    }

private:
    std::vector<int> extents_;
    bool block_indexed_;

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
// Lossless block-layout relayouts (Reblock, and the repack Interleave below).
//
// DESIGN NOTE (intended library form, deferred): the clean, general shape for
// these is a *dimension-general* block-relayout -- a component that splits and
// permutes arbitrary index dimensions, carrying any trailing dims through
// untouched (the way the Python research sketch's BlockLayout(splits=...)/
// SplitStorage do, via Halide's `_` placeholder), quantizing/packing whatever
// falls out. That would let a single `BlockLayout` utility live in the core
// Approximation library and compose in front of *any* lossy quant, with the
// repack interleave being just one instantiation. We deliberately do NOT do
// that here: `Func`/`Var` `_` wildcard semantics were a source of trouble, and
// pinning them down is its own rabbit hole. Instead these relayouts are
// written at fixed, concrete arities (folding any extra "lane" -- e.g. repack's
// 4 interleaved rows -- into the block index blk), reusing the existing
// (kk, blk) quant/pack components unchanged. When the wildcard story is sorted,
// these should graduate to the dimension-general form.
// ---------------------------------------------------------------------------

// Losslessly re-view a block-indexed Func at a different block size (the flat
// element order is unchanged; only the (kk, blk) factorization differs).
// decode(): (kk_from, blk_from) at `from_block` -> (kk_to, blk_to) at
// `to_block`, reading the same global element g = blk_to*to_block + kk_to from
// its source position (g % from_block, g / from_block). encode() is the
// mirror. This is what lets a vec_dot present an activation stored in its own
// (smaller) block size -- e.g. Q8_0's 32-element blocks -- at a weight's
// (larger) block size (Q1_0's 128, NVFP4's 64), so both operands share one
// (kk, blk) and the Generator's reduction stays uniform. The block-structure
// reconciliation lives here, in an Approximation, not open-coded in the
// reduction.
class Reblock : public Halide::Approximation {
public:
    Reblock(int from_block, int to_block)
        : from_(from_block), to_(to_block) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func in = inputs[0];  // (kk, blk) at to_block
        Var kk("kk"), blk("blk");
        Expr g = blk * from_ + kk;
        Func out("reblock_encoded");
        out(kk, blk) = in(g % to_, g / to_);
        return {{out}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func in = encoded[0];  // (kk, blk) at from_block
        Var kk("kk"), blk("blk");
        Expr g = blk * to_ + kk;
        Func out("reblock_decoded");
        out(kk, blk) = in(g % from_, g / from_);
        return {{out}, {}};
    }

private:
    int from_, to_;
};

// An activation codec that decodes to (kk, blk) at `to_block`: `act_codec`
// (block-indexed at the activation's own `from_block`) composed with a Reblock
// when the two differ, else `act_codec` unchanged. Lets a vec_dot pair a weight
// of one block size against an activation of another (Q1_0/NVFP4 x Q8_0).
inline std::unique_ptr<Halide::Approximation> reblock_activation(
    std::unique_ptr<Halide::Approximation> act_codec, int from_block, int to_block) {
    if (from_block == to_block) {
        return act_codec;
    }
    return std::make_unique<Halide::Compose>(std::move(act_codec), Reblock{from_block, to_block});
}

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
        auto define_extreme_signed_stat = [&]() {
            stat(blk) = Tuple(0.0f, 0.0f);  // {amax, extreme_signed}
            Expr v = block(r, blk);
            Expr take = abs(v) > stat(blk)[0];
            stat(blk) = Tuple(select(take, abs(v), stat(blk)[0]),
                              select(take, v, stat(blk)[1]));
        };
        if (anchor_ == ScaleAnchor::AbsMax) {
            stat(blk) = 0.0f;
            stat(blk) = max(stat(blk), abs(block(r, blk)));
            scale(blk) = stat(blk) / (float)qmax_;
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == ScaleAnchor::ExtremeSignedValue) {
            define_extreme_signed_stat();
            scale(blk) = stat(blk)[1] * (-1.0f / (float)qmax_);
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == ScaleAnchor::MeanAbs) {
            stat(blk) = 0.0f;
            stat(blk) += abs(block(r, blk));
            scale(blk) = stat(blk) / (float)block_size_;
            id(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else {  // ExtremeSignedValueTwoStep
            define_extreme_signed_stat();
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

// Assemble a little-endian integer word starting at bytes(base, blk). The
// uint32 specialization below is the on-disk byte order shared by F32Pack's
// scale, FiveBitPack's qh, and IQ3_XXS's aux32.
inline Halide::Expr le_uint(Halide::Func bytes, Halide::Expr base, Halide::Var blk, int byte_count) {
    using namespace Halide;
    Expr result = cast<uint32_t>(0);
    for (int i = 0; i < byte_count; i++) {
        result = result | (cast<uint32_t>(bytes(base + i, blk)) << (8 * i));
    }
    return result;
}

inline Halide::Expr le_u32(Halide::Func bytes, Halide::Expr base, Halide::Var blk) {
    using namespace Halide;
    return le_uint(bytes, base, blk, 4);
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
        Func bytes = encoded[0];  // bytes(byte, blk[, ...]), byte in [0, 2)
        Var blk("blk");
        Expr lo = cast<uint16_t>(bytes(0, blk, _));
        Expr hi = cast<uint16_t>(bytes(1, blk, _));
        Func scale("fp16_pack_scale");
        scale(blk, _) = cast<float>(reinterpret<float16_t>(lo | (hi << 8)));
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
        Func bytes = encoded[0];  // bytes(byte, blk[, ...]), byte in [0, 4)
        Var blk("blk");
        // Dimension-general via Halide::_ (matches Fp16Pack): any trailing
        // "lane" dims -- e.g. a repack weight's columns -- ride through, so this
        // pack can decode a repack scale field, not just a plain (byte, blk) one.
        Expr b0 = cast<uint32_t>(bytes(0, blk, _)), b1 = cast<uint32_t>(bytes(1, blk, _));
        Expr b2 = cast<uint32_t>(bytes(2, blk, _)), b3 = cast<uint32_t>(bytes(3, blk, _));
        Func scale("f32_pack_scale");
        scale(blk, _) = reinterpret<float>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
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

// decode(1 byte) -> float, an E8M0 power-of-two exponent (GGML's MXFP4/NVFP4
// scale format). Reproduces ggml_e8m0_to_fp32_half's exact bit construction:
// d = 2^(e-128) for every e in [0, 255], computed via a subnormal-exploiting
// shift trick for e<2 instead of a normal exponent-field write (both branches
// compute the same uniform 2^(e-128); see the comment inline). Decode-only:
// MXFP4 quantize is extern-delegated (see ExternQuantize).
class E8M0Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "E8M0Pack is decode-only -- quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func byte = encoded[0];  // byte(byte_idx, blk[, ...]), byte_idx in [0, 1)
        Var blk("blk");
        // Dimension-general via Halide::_ (matches Fp16Pack/F32Pack) so this pack
        // can decode a repack weight's E8M0 scale header, columns riding through.
        Expr e = cast<uint32_t>(byte(0, blk, _));
        Expr bits = select(e < 2, cast<uint32_t>(0x00200000) << e, (e - 1) << 23);
        Func scale("e8m0_pack_scale");
        scale(blk, _) = reinterpret<float>(bits);
        return {{scale}, {}};
    }
};

// decode(1 byte per sub-block) -> float(sub, blk), a UE4M3 unsigned
// 4-exponent/3-mantissa float (GGML's NVFP4 per-sub-block scale format).
// Reproduces ggml_ue4m3_to_fp32_half's exact construction: subnormal
// (exp==0) is man/512, normal is (1+man/8)*2^(exp-7), both halved; byte 0x00
// or 0x7f (GGML's NVFP4 zero/sentinel bytes) decode to 0. Unlike
// Fp16Pack/E8M0Pack (exactly one scale value per block), this is meant to be
// used with ScaleDequant's `num_scales` > 1: the Funcs here are indexed by
// `sub` directly (one byte each). Decode-only: NVFP4 quantize is
// extern-delegated (see ExternQuantize).
class UE4M3Pack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "UE4M3Pack is decode-only -- quantize is deferred to an ExternQuantize.\n";
        return {};
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
//
// `plane_axis` (default false) selects an alternate decode that keeps `plane`
// as an explicit LEADING output axis instead of folding it into a flat kk:
//   fields(plane, pos, blk, _) = (bytes(pos, blk, _) >> plane*field_bits) & mask
// This is the shape a *combined* (scale, min) field wants -- plane 0 = scale,
// plane 1 = min -- so TwoLevelScaleDequant can read both from one func by its
// plane index. It's dimension-general (Halide::_) and needs neither `outer` nor
// `qmax` (raw unsigned fields), so it is exactly Q2_K's per-sub-block nibble-
// pair (scale, min) byte array, with no bespoke leaf. Decode-only in this mode
// (only ever an extern-delegated TrustedInverse decoder stage).
class PlanarBitPack : public Halide::Approximation {
public:
    PlanarBitPack(int field_bits, int plane_count, int pos_count, int qmax, bool plane_axis = false)
        : field_bits_(field_bits), plane_count_(plane_count), pos_count_(pos_count), qmax_(qmax),
          plane_axis_(plane_axis) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        if (plane_axis_) {
            _halide_user_error << "PlanarBitPack plane-axis mode is decode-only "
                                  "(only an extern-delegated TrustedInverse decoder stage).\n";
            return {};
        }
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
        if (plane_axis_) {
            Var plane("plane"), pos("pos");
            Expr field = (cast<uint32_t>(bytes(pos, blk, _)) >> (cast<uint32_t>(plane) * field_bits_)) &
                         ((1u << field_bits_) - 1);
            Func fields("planar_bit_pack_fields");
            fields(plane, pos, blk, _) = cast<uint8_t>(field);
            return {{fields}, {}};
        }
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
    bool plane_axis_;
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

// decode(52 bytes {qs[48]; qh[4]}) -> codes(kk, blk), the raw base-3 digit in
// [0, 3) -- GGML's TQ1_0 packing: 256 elements in 3 sections (a 160-element
// and an 80-element run at 5 trits/byte, then a 16-element run at 4 real
// trits/byte, its 5th digit always 0). decode() reverses the ceiling-division
// packing (`byte = ceil(digit_number * 256 / 243)`) via the same
// multiply-truncate-rescale trick tq1_0_generators.cpp hand-rolled: extracting
// digit `n` needs multiplier `3^n`, `n` from the most-significant digit. Codes
// feed a codebook index directly (TQ2_0's {-1, 0, 1, unused} convention).
// Decode-only: TQ1_0 quantize is extern-delegated.
class TritPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "TritPack is decode-only -- quantize is deferred to an ExternQuantize.\n";
        return {};
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
    return cast<float>(reinterpret<float16_t>(cast<uint16_t>(le_uint(bytes, offset, blk, 2))));
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

template<size_t N>
inline Halide::Buffer<int8_t> make_static_codebook(const int8_t (&values)[N], const char *name) {
    return Halide::Buffer<int8_t>(const_cast<int8_t *>(values), (int)N, name);
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

// The encoder half for formats that have NO forward map at all (the IQ1/IQ2
// importance-matrix-only quantizers: GGML exposes no *_quantize_via_ggml for
// them). encode() produces a correctly-shaped `blocks(byte, blk)` uint8 Func
// so that Func::approximate_by() -- which always builds encode() before
// decode() -- can splice the round trip; the value is a placeholder (0),
// because Pipeline::compute_offline() always severs this encode and binds the
// real already-quantized Input in its place, so it is never computed. This is
// what lets a decode-only format still go through the standard
// approximate_by/compute_offline path (exercising the framework) instead of a
// bespoke direct-decode generator. decode() traps -- the paired decoder half
// of the TrustedInverse owns dequantize.
class SeveredEncode : public Halide::Approximation {
public:
    // `dims` is the dimensionality of the packed buffer this stands in for: 2
    // for a plain (byte, blk) codec, 3 for a repack weight buffer
    // (byte, k-block, col-group).
    explicit SeveredEncode(int block_bytes, int dims = 2)
        : block_bytes_(block_bytes), dims_(dims) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        using namespace Halide;
        std::vector<Var> args;
        for (int d = 0; d < dims_; d++) {
            args.push_back(Var("se" + std::to_string(d)));
        }
        Func blocks("severed_encode_blocks");
        blocks(args) = cast<uint8_t>(0);
        return {{blocks}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func>) override {
        _halide_user_error << "SeveredEncode::decode is never valid -- it is only "
                              "the (always-severed) encoder half of a TrustedInverse.\n";
        return {};
    }

private:
    int block_bytes_, dims_;
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
        // Dimension-general (pure) decode: the Halide::_ placeholder carries
        // any extra trailing "lane" dims (e.g. a matmul weight's column dims)
        // through untouched; with zero trailing dims it collapses to the
        // familiar (kk, blk). See the DESIGN NOTE by Reblock.
        Func values("codebook_values");
        values(kk, blk, _) = table(clamp(cast<int32_t>(codes(kk, blk, _)), 0, table.dim(0).extent() - 1));
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
        // Dimension-general via Halide::_: any trailing "lane" dims (a matmul
        // weight's columns) ride through untouched; zero of them collapses to
        // the familiar (kk, blk). See the DESIGN NOTE by Reblock.
        Func dequantized("scale_dequantized");
        if (num_scales_ == 1) {
            dequantized(kk, blk, _) = cast<float>(cast<int32_t>(codes(kk, blk, _))) * scale(blk, _);
        } else {
            int sub_size = block_size_ / num_scales_;
            dequantized(kk, blk, _) = cast<float>(cast<int32_t>(codes(kk, blk, _))) * scale(kk / sub_size, blk, _);
        }
        return {{dequantized}, {}};
    }

private:
    int block_size_, num_scales_;
};

// The K-quant / IQ4_XS two-level dequantize: a super-block-wide float `d`
// (and, for the affine K-quants, `dmin`) times a per-sub-block scale (and
// min). Inputs, in order:
//   has_min:  {d, dmin, scale_min, codes}
//   no min:   {d, scale, codes}
// `has_min` selects whether the dmin/scale_min affine pair is present. When it
// is, scale and min arrive *combined* in one func `scale_min(plane, sub, ...)`
// -- plane 0 = scale, plane 1 = min -- the shape PlanarBitPack's plane-axis
// mode and K4ScaleMinPack both produce, so a single field carries both halves
// of the affine per-sub-block parameters (no separate `min` slot to thread).
// `codes` may be raw integer codes (K-quants) or already-looked-up codebook
// values (IQ4_XS, via a Codebook stage). scale/min are indexed per sub-block
// (kk / sub_size). Decode-only, same rationale as ScaleDequant.
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
            Func d = encoded[0], dmin = encoded[1], scale_min = encoded[2], codes = encoded[3];
            Expr sub = kk / sub_size_;
            dequantized(kk, blk, _) = d(blk, _) * cast<float>(scale_min(0, sub, blk, _)) * cast<float>(codes(kk, blk, _)) -
                                      dmin(blk, _) * cast<float>(scale_min(1, sub, blk, _));
        } else {
            Func d = encoded[0], scale = encoded[1], codes = encoded[2];
            dequantized(kk, blk, _) = d(blk, _) * cast<float>(scale(kk / sub_size_, blk, _)) * cast<float>(codes(kk, blk, _));
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

// (Q2_K's per-sub-block nibble-pair scale/min -- low nibble = scale, high
// nibble = min -- is no longer a bespoke leaf: it is exactly PlanarBitPack's
// plane-axis mode, PlanarBitPack{4, 2, 16, 0, /*plane_axis=*/true}, producing
// the same combined (plane, sub) field K4ScaleMinPack does. See make_q2_k_scheme.)

// decode(bytes(byte_idx, blk), 12 bytes) -> scale_min(plane, sub, blk) for sub
// in [0, 8), plane 0 = scale / plane 1 = min -- GGML's get_scale_min_k4 scheme,
// shared by Q4_K and Q5_K: for sub<4, scale/min are simply the low 6 bits of
// byte[sub]/byte[sub+4]; for sub>=4, each is a 4-bit low part from byte[sub+4]
// combined with a 2-bit high part borrowed from the top 2 bits of an
// earlier byte (byte[sub-4] for scale, byte[sub] for min) -- a
// bit-interleaved packing that fits 8 six-bit values into 6 bytes' worth of
// budget instead of 8 (see q4_k_generators.cpp's original header comment for
// the full derivation). Emits the combined (plane, sub) field TwoLevelScaleDequant
// consumes. Decode-only: Q4_K/Q5_K quantize is extern-delegated.
class K4ScaleMinPack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "K4ScaleMinPack is decode-only -- quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk[, ...]), byte_idx in [0, 12)
        Var plane("plane"), sub("sub"), blk("blk");

        Expr jj = clamp(sub - 4, 0, 3);
        Expr sc = select(sub < 4,
                         bytes(sub, blk, _) & 0x3f,
                         cast<uint8_t>((bytes(8 + jj, blk, _) & 0x0f) | ((bytes(jj, blk, _) >> 6) << 4)));
        Expr m = select(sub < 4,
                        bytes(sub + 4, blk, _) & 0x3f,
                        cast<uint8_t>((bytes(8 + jj, blk, _) >> 4) | ((bytes(4 + jj, blk, _) >> 6) << 4)));

        Func scale_min("k4_scale_min_pack");
        scale_min(plane, sub, blk, _) = cast<uint8_t>(select(plane == 0, sc, m));
        return {{scale_min}, {}};
    }
};

// decode(bytes(byte_idx, blk), 12 bytes) -> scale(sub, blk) for sub in
// [0, 16) -- Q3_K's 16 SIGNED 6-bit scale values (no min field), a
// different bit-interleaving than get_scale_min_k4 above: the 2 high bits
// always live in byte (sub%4)+8, at bit-shift 2*(sub/4); the 4 low bits
// live in byte (sub%8), taken from the byte's low nibble if sub<8 or high
// nibble if sub>=8. The final signed value is (low|(high<<4)) - 32 (see
// q3_k_generators.cpp's original header comment for the full derivation).
// Decode-only: Q3_K quantize is extern-delegated.
class Q3KScalePack : public Halide::Approximation {
public:
    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "Q3KScalePack is decode-only -- quantize is deferred to an ExternQuantize.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];  // bytes(byte_idx, blk[, ...]), byte_idx in [0, 12)
        Var sub("sub"), blk("blk");

        // Dimension-general via Halide::_, matching the other scale unpackers.
        Expr low_byte_idx = sub % 8;
        Expr use_high_nibble = sub >= 8;
        Expr low_byte = bytes(low_byte_idx, blk, _);
        Expr low_val = select(use_high_nibble, cast<int32_t>(low_byte >> 4), cast<int32_t>(low_byte & 0x0f));
        Expr high_byte_idx = (sub % 4) + 8;
        Expr high_shift = (sub / 4) * 2;
        Expr high = cast<int32_t>((bytes(high_byte_idx, blk, _) >> high_shift) & 0x3);

        Func scale("q3k_scale_pack_scale");
        scale(sub, blk, _) = cast<int8_t>((low_val | (high << 4)) - 32);
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

// The four IQ1/IQ2 importance-matrix-only formats have no from_float extern
// (GGML exposes no *_quantize_via_ggml), so their make_*_scheme() below pairs
// this decode leaf with a SeveredEncode via TrustedInverse (a dequantize-only /
// vec_dot-only round trip; the placeholder encode is always severed). Each is a
// verified-unchanged transcription of the matching *_generators.cpp dequantize,
// reading bytes(byte, blk) and emitting the {8,4,8} superblock form
// (j/j-elem, l, group, blk) so the composed BlockReshape does the reshape.

// IQ2_XS: 74-byte block. qs[32] as 32 little-endian uint16 (4 per group): low
// 9 bits index the 512-entry uint64 iq2xs_grid, top 7 bits index ksigns_iq2xs;
// scale a nibble byte array (2 l's/byte) via d*(0.5+nibble)*0.25.
class IQ2XSGridDequantize : public Halide::Approximation {
public:
    IQ2XSGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq2xs_grid, 512, "iq2xs_grid")),
          ksigns_(make_grid_buffer(iq_grids::ksigns_iq2xs, 128, "ksigns_iq2xs")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ2XSGridDequantize is decode-only.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var j("j"), l("l"), ib32("ib32"), blk("blk");
        constexpr int kQsOffset = 2;
        constexpr int kScalesOffset = 66;

        Expr qs_idx = ib32 * 4 + l;
        Expr q_lo = cast<uint16_t>(bytes(kQsOffset + qs_idx * 2 + 0, blk));
        Expr q_hi = cast<uint16_t>(bytes(kQsOffset + qs_idx * 2 + 1, blk));
        Expr qs_val = cast<uint16_t>(q_lo | (q_hi << 8));
        Expr grid_idx = clamp(cast<int32_t>(qs_val & 511), 0, 511);
        Expr sign_idx = clamp(cast<int32_t>(qs_val >> 9), 0, 127);

        Expr scales_byte = bytes(kScalesOffset + ib32, blk);
        Expr nibble = select(l < 2, scales_byte & 0x0f, scales_byte >> 4);
        Expr db = fp16_delta(bytes, 0, blk) * (0.5f + cast<float>(nibble)) * 0.25f;

        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Buffer<uint8_t> ksigns = ksigns_;
        Expr signs = ksigns(sign_idx);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j)) != 0;

        Func dequantized("iq2xs_grid_dequantized");
        dequantized(j, l, ib32, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);
        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint64_t> grid_;
    Halide::Buffer<uint8_t> ksigns_;
};

// IQ2_XXS: 66-byte block. Per group, an 8-byte window: bytes 0..3 are 4 grid
// indices (one per l) into the 256-entry uint64 iq2xxs_grid; bytes 4..7 form a
// uint32 aux32 whose top 4 bits are a scale exponent (d*(0.5+exp)*0.25) and
// whose low 28 bits pack four 7-bit ksigns indices.
class IQ2XXSGridDequantize : public Halide::Approximation {
public:
    IQ2XXSGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq2xxs_grid, 256, "iq2xxs_grid")),
          ksigns_(make_grid_buffer(iq_grids::ksigns_iq2xs, 128, "ksigns_iq2xs")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ2XXSGridDequantize is decode-only.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var j("j"), l("l"), ib32("ib32"), blk("blk");
        constexpr int kQsOffset = 2;

        Expr grid_idx = clamp(cast<int32_t>(bytes(kQsOffset + ib32 * 8 + l, blk)), 0, 255);
        Expr aux32 = le_u32(bytes, kQsOffset + ib32 * 8 + 4, blk);
        Expr db = fp16_delta(bytes, 0, blk) * (0.5f + cast<float>(aux32 >> 28)) * 0.25f;
        Expr sign_idx = clamp(cast<int32_t>((aux32 >> (cast<uint32_t>(l) * 7)) & 127), 0, 127);

        Buffer<uint8_t> ksigns = ksigns_;
        Expr signs = ksigns(sign_idx);
        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_byte = cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff);
        Expr sign_bit = (cast<uint32_t>(signs) & (cast<uint32_t>(1) << j)) != 0;

        Func dequantized("iq2xxs_grid_dequantized");
        dequantized(j, l, ib32, blk) = db * cast<float>(grid_byte) * select(sign_bit, -1.0f, 1.0f);
        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint64_t> grid_;
    Halide::Buffer<uint8_t> ksigns_;
};

// IQ1_S: 50-byte block. qs[32] low grid-index bytes (one per l); qh[8] as 8
// uint16 (one per group): 3 bits/l give the grid index's high 3 bits, bits
// 12..14 a per-group scale (dl = d*(2*s+1)), bit 15 selects a +/-IQ1S_DELTA
// added to every value. iq1s_grid entries are SIGNED bytes used directly.
class IQ1SGridDequantize : public Halide::Approximation {
public:
    IQ1SGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq1s_grid, 2048, "iq1s_grid")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ1SGridDequantize is decode-only.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var j("j"), l("l"), ib("ib"), blk("blk");
        constexpr float kIQ1S_DELTA = 0.125f;
        constexpr int kQsOffset = 2;
        constexpr int kQhOffset = 34;

        Expr qh_lo = cast<uint16_t>(bytes(kQhOffset + ib * 2 + 0, blk));
        Expr qh_hi = cast<uint16_t>(bytes(kQhOffset + ib * 2 + 1, blk));
        Expr qh_val = cast<uint16_t>(qh_lo | (qh_hi << 8));
        Expr dl_scale = cast<int32_t>((qh_val >> 12) & 7);
        Expr dl = fp16_delta(bytes, 0, blk) * cast<float>(2 * dl_scale + 1);
        Expr delta = select((qh_val & 0x8000) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

        Expr qs_byte = bytes(kQsOffset + ib * 4 + l, blk);
        Expr high3 = cast<uint32_t>((qh_val >> (cast<uint16_t>(l) * 3)) & 7);
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + (high3 << 8))), 0, 2047);

        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_signed = reinterpret<int8_t>(cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff));

        Func dequantized("iq1s_grid_dequantized");
        dequantized(j, l, ib, blk) = dl * (cast<float>(grid_signed) + delta);
        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint64_t> grid_;
};

// IQ1_M: 56-byte block, NO separate delta field. qs[32] low index bytes;
// qh[16] (2/group) give a high-3-bit grid extension + a sign-delta bit per l2;
// scales[8] as 4 uint16: the block's shared fp16 d is bit-gathered from the
// top nibble of all 4 words, each word's low 12 bits holding two 3-bit
// per-group scales. Signed codebook + /-IQ1S_DELTA, same as IQ1_S.
class IQ1MGridDequantize : public Halide::Approximation {
public:
    IQ1MGridDequantize()
        : grid_(make_grid_buffer(iq_grids::iq1s_grid, 2048, "iq1s_grid")) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "IQ1MGridDequantize is decode-only.\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func bytes = encoded[0];
        Var j("j"), l2("l2"), ib("ib"), blk("blk");
        constexpr float kIQ1S_DELTA = 0.125f;
        constexpr int kQsOffset = 0;
        constexpr int kQhOffset = 32;
        constexpr int kScalesOffset = 48;

        auto sc_word = [&](int k) -> Expr {
            Expr lo = cast<uint32_t>(bytes(kScalesOffset + k * 2 + 0, blk));
            Expr hi = cast<uint32_t>(bytes(kScalesOffset + k * 2 + 1, blk));
            return lo | (hi << 8);
        };
        Expr sc0 = sc_word(0), sc1 = sc_word(1), sc2 = sc_word(2), sc3 = sc_word(3);
        Expr d_bits = (sc0 >> 12) | ((sc1 >> 8) & 0xf0) | ((sc2 >> 4) & 0xf00) | (sc3 & 0xf000);
        Expr d = cast<float>(reinterpret<float16_t>(cast<uint16_t>(d_bits)));

        Expr qh_half = l2 / 2;
        Expr parity = l2 % 2;
        Expr qh_byte = cast<uint32_t>(bytes(kQhOffset + ib * 2 + qh_half, blk));
        Expr qs_byte = bytes(kQsOffset + ib * 4 + l2, blk);
        // Constant-amount shifts per parity arm (a variable-amount shift into a
        // buffer index defeats Halide bounds inference even when masked).
        Expr extra_bits = select(parity == 0, (qh_byte << 8) & 0x700, (qh_byte << 4) & 0x700);
        Expr grid_idx = clamp(cast<int32_t>(cast<uint16_t>(cast<uint32_t>(qs_byte) + extra_bits)), 0, 2047);

        Expr delta_mask = select(parity == 0, cast<uint32_t>(0x08), cast<uint32_t>(0x80));
        Expr delta = select((qh_byte & delta_mask) != 0, -kIQ1S_DELTA, kIQ1S_DELTA);

        Expr sc_idx = ib / 2;
        Expr sc_word_val = select(sc_idx == 0, sc0, select(sc_idx == 1, sc1, select(sc_idx == 2, sc2, sc3)));
        Expr shift = (ib % 2) * 6 + qh_half * 3;
        Expr scale3 = (sc_word_val >> cast<uint32_t>(shift)) & 7;
        Expr dl = d * cast<float>(2 * scale3 + 1);

        Buffer<uint64_t> grid = grid_;
        Expr grid_val = grid(grid_idx);
        Expr grid_signed = reinterpret<int8_t>(cast<uint8_t>((grid_val >> (cast<uint64_t>(j) * 8)) & 0xff));

        Func dequantized("iq1m_grid_dequantized");
        dequantized(j, l2, ib, blk) = dl * (cast<float>(grid_signed) + delta);
        return {{dequantized}, {}};
    }

private:
    Halide::Buffer<uint64_t> grid_;
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

        // Dimension-general via Halide::_, matching the other scale unpackers.
        // ls = (scales_l[sub/2] >> 4*(sub%2)) & 0xf | ((scales_h >> 2*sub) & 3) << 4
        Expr low4 = cast<int32_t>((scales_l(sub / 2, blk, _) >> ((sub % 2) * 4)) & 0x0f);
        Expr sh_lo = cast<uint16_t>(scales_h(0, blk, _));
        Expr sh_hi = cast<uint16_t>(scales_h(1, blk, _));
        Expr sh = sh_lo | (sh_hi << 8);
        Expr high2 = cast<int32_t>((sh >> cast<uint16_t>(sub * 2)) & 3);
        Expr ls = low4 | (high2 << 4);

        Func scale("iq4xs_scale");
        scale(sub, blk, _) = cast<int8_t>(ls - 32);
        return {{scale}, {}};
    }
};

// ---------------------------------------------------------------------------
// 7. Repack: interleaved multi-row activation layout (block_q8_0x4 / q8_Kx4).
//
// These are repack-specific instances of the deferred general block-relayout
// (see the DESIGN NOTE by Reblock): "block-layout change prior to applying the
// same lossy quantization Approximations". The 4 interleaved rows are folded
// into the block index blk = ib*4 + row, so the existing (kk, blk)
// SymmetricAffineQuantize + BytePack + Fp16Pack run unchanged; only the two
// relayouts here (input row-blocking and output interleave/header assembly)
// are new. n_rows is fixed at 4 (repack's "x4").
// ---------------------------------------------------------------------------

// Losslessly re-view a 2-D activation x(col, row in [0,4)) as block-indexed
// block(kk in [0, block_size), blk = ib*4 + row), where ib is the k-block. The
// per-row scale then falls out of the quantizer as a per-blk scale.
class RepackRowReshape : public Halide::Approximation {
public:
    explicit RepackRowReshape(int block_size)
        : block_size_(block_size) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func x = inputs[0];  // x(col, row)
        Var kk("kk"), blk("blk");
        Func block("repack_row_block");
        block(kk, blk) = x((blk / 4) * block_size_ + kk, blk % 4);
        return {{block}, {}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func block = encoded[0];  // block(kk, blk)
        Var col("col"), row("row");
        Func x("repack_row_x");
        x(col, row) = block(col % block_size_, (col / block_size_) * 4 + row);
        return {{x}, {}};
    }

private:
    int block_size_;
};

// Assemble one interleaved output block (byte, ib) from the per-(row) packed
// code bytes and scale bytes of the 4 rows blk = ib*4 + row. Header: 4 deltas
// (`delta_bytes` each, row order). Payload: codes interleaved in groups of
// `blocklen` -- payload position jj -> (row = (jj % (4*bl))/bl,
// kk = (jj/(4*bl))*bl + jj%bl), matching GGML's src_id/src_offset.
class RepackInterleavePack : public Halide::Approximation {
public:
    RepackInterleavePack(int block_size, int blocklen, int delta_bytes, bool with_bsums = false)
        : block_size_(block_size), blocklen_(blocklen), delta_bytes_(delta_bytes), with_bsums_(with_bsums) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func> inputs) override {
        using namespace Halide;
        Func code = inputs[0], scale = inputs[1];
        Var byte("byte"), ib("ib");
        int header = 4 * delta_bytes_;
        int payload_end = header + block_size_ * 4;

        Expr row_d = clamp(byte / delta_bytes_, 0, 3);
        Expr delta_byte = scale(clamp(byte % delta_bytes_, 0, delta_bytes_ - 1), ib * 4 + row_d);

        Expr jj = clamp(byte - header, 0, block_size_ * 4 - 1);
        Expr row_p = (jj % (4 * blocklen_)) / blocklen_;
        Expr kk_p = (jj / (4 * blocklen_)) * blocklen_ + (jj % blocklen_);
        Expr code_byte = code(kk_p, ib * 4 + row_p);

        Func blocks("repack_interleave_blocks");
        if (!with_bsums_) {
            blocks(byte, ib) = select(byte < header, delta_byte, code_byte);
            return {{blocks}, {}};
        }

        // Q8_K's block_q8_Kx4 appends `bsums`: int16 group-sums of the int8
        // codes, scattered across rows/groups by GGML's index_q8_k mapping.
        // Reduce over the interleaved payload order (rj), same as GGML.
        Var g("g");
        Func bsums("repack_bsums");
        RDom rj(0, block_size_ * 4, "rj");
        Expr rp = (rj % (4 * blocklen_)) / blocklen_;
        Expr kp = (rj / (4 * blocklen_)) * blocklen_ + (rj % blocklen_);
        Expr qval = reinterpret<int8_t>(code(kp, ib * 4 + rp));
        int shift = blocklen_ == 8 ? 3 : 2;  // log2(blocklen)
        Expr idx = (((rj & (4 * blocklen_ - 1)) >> shift) << 2) + ((rj >> 8) << 4) + ((rj >> 6) & 3);
        bsums(g, ib) = cast<int32_t>(0);
        bsums(idx, ib) += cast<int32_t>(qval);

        int nbsum = (block_size_ / 16) * 4;  // 64 groups
        Expr bsum_rel = clamp(byte - payload_end, 0, nbsum * 2 - 1);
        Expr bsum_g = bsum_rel / 2;
        Expr bsum_is_lo = (bsum_rel % 2) == 0;
        Expr bsum_bits = reinterpret<uint16_t>(cast<int16_t>(bsums(bsum_g, ib)));
        Expr bsum_byte = cast<uint8_t>(select(bsum_is_lo, bsum_bits & 0xff, (bsum_bits >> 8) & 0xff));

        blocks(byte, ib) = select(byte < header, delta_byte, byte < payload_end, code_byte, bsum_byte);
        return {{blocks}, {bsums}};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func blocks = encoded[0];
        Var kk("kk"), blk("blk"), sbyte("sbyte");
        int header = 4 * delta_bytes_;

        // blk = ib*4 + row: ib = blk/4, row = blk%4.
        Func code("repack_interleave_code");
        Expr jj = (kk / blocklen_) * (4 * blocklen_) + (blk % 4) * blocklen_ + (kk % blocklen_);
        code(kk, blk) = blocks(header + jj, blk / 4);
        Func scaleb("repack_interleave_scale");
        scaleb(sbyte, blk) = blocks((blk % 4) * delta_bytes_ + sbyte, blk / 4);
        return {{code, scaleb}, {}};
    }

private:
    int block_size_, blocklen_, delta_bytes_;
    bool with_bsums_;
};

// block_q8_0x4 codec: block-layout relayout + the same symmetric Q8_0 quantize
// (amax/127, round) as make_symmetric_block_codec, interleaved by `blocklen`.
inline std::unique_ptr<Halide::Approximation> make_q8_0x4_scheme(int blocklen) {
    using namespace Halide;
    return std::make_unique<Compose>(
        RepackInterleavePack{32, blocklen, /*delta_bytes=*/2},
        Apply{0, 1, 1, BytePack{}},  // codes -> bytes
        Apply{1, 1, 1, Fp16Pack{}},  // scale -> fp16 bytes
        SymmetricAffineQuantize{32, 127, RoundingMode::Nearest, ScaleAnchor::AbsMax},
        RepackRowReshape{32});
}

// block_q8_Kx4 codec: same shape as make_q8_0x4_scheme but a 256-element block,
// a float32 delta per row (F32Pack), the same round-to-even -127/max Q8_K
// quantize as make_q8_k_codec, and the interleaved bsums field (with_bsums).
inline std::unique_ptr<Halide::Approximation> make_q8_kx4_scheme(int blocklen) {
    using namespace Halide;
    return std::make_unique<Compose>(
        RepackInterleavePack{256, blocklen, /*delta_bytes=*/4, /*with_bsums=*/true},
        Apply{0, 1, 1, BytePack{}},  // codes -> bytes
        Apply{1, 1, 1, F32Pack{}},   // scale -> float32 bytes
        SymmetricAffineQuantize{256, 127, RoundingMode::NearestEvenClampedHigh,
                                ScaleAnchor::ExtremeSignedValueTwoStep},
        RepackRowReshape{256});
}

// ---------------------------------------------------------------------------
// 8. Repack weight un-interleave (for gemv/gemm): decode the 3-D interleaved
// weight buffer (byte, k-block, col-group) into per-element codes + per-column
// scale *bytes* (the composed Fp16/F32/E8M0 pack turns those into the float
// scale -- no scale decode duplicated in the leaf), carrying the two column
// dims (col-in-group j, col-group x) as explicit trailing dims so the
// dimension-general ScaleDequant/Codebook/scale packs (Halide::_) run unchanged
// and the matmul reduces over (kk, blk). Decode-only (the weight is
// pre-quantized; SeveredEncode is the severed encoder half). This is the
// decode twin of RepackInterleavePack, for the four "simple" weight families.
// ---------------------------------------------------------------------------
enum class RepackWeightCode { SignedByte,    // Q8_0: whole signed int8
                              SignedNibble,  // Q4_0: two's-complement 4-bit (repack's XOR-0x8)
                              RawNibble };   // IQ4_NL/MXFP4: raw 4-bit codebook index
enum class RepackWeightScale { Fp16,         // 2*n_cols-byte fp16 delta header
                               E8M0,         // n_cols-byte E8M0 exponent header
                               F32 };        // 4*n_cols-byte float32 delta header (Q8_Kx4 activation)

class UnInterleaveWeight : public Halide::Approximation {
public:
    UnInterleaveWeight(int n_cols, int blocklen, int block_size,
                       RepackWeightCode code_kind, RepackWeightScale scale_kind)
        : n_cols_(n_cols), blocklen_(blocklen), block_size_(block_size),
          code_kind_(code_kind), scale_kind_(scale_kind) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "UnInterleaveWeight is decode-only (SeveredEncode is the "
                              "severed encoder half of its TrustedInverse).\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func blocks = encoded[0];  // blocks(byte, k-block, col-group)
        Var byte("byte"), kk("kk"), blk("blk"), j("j"), x("x");
        // Per-column scale header width: fp16 = 2, f32 = 4, e8m0 = 1 byte/column.
        int scale_stride = scale_kind_ == RepackWeightScale::Fp16 ? 2 : scale_kind_ == RepackWeightScale::F32 ? 4 :
                                                                                                                1;
        int header = scale_stride * n_cols_;

        Func codes("uninterleave_codes");
        if (code_kind_ == RepackWeightCode::SignedByte) {
            Expr qs_idx = (kk / blocklen_) * n_cols_ * blocklen_ + j * blocklen_ + (kk % blocklen_);
            codes(kk, blk, j, x) = reinterpret<int8_t>(blocks(header + qs_idx, blk, x));
        } else {
            Expr half = kk / (block_size_ / 2);
            Expr el = kk % (block_size_ / 2);
            Expr qs_idx = (el / blocklen_) * n_cols_ * blocklen_ + j * blocklen_ + (el % blocklen_);
            Expr byte_v = blocks(header + qs_idx, blk, x);
            Expr nib = cast<int32_t>(select(half == 0, byte_v & 0x0f, (byte_v >> 4) & 0x0f));
            Expr code = code_kind_ == RepackWeightCode::SignedNibble ? select(nib < 8, nib, nib - 16) : nib;
            codes(kk, blk, j, x) = cast<int8_t>(code);
        }

        // Pure addressing: gather column j's raw scale-header bytes; the composed
        // Fp16Pack / F32Pack / E8M0Pack (dimension-general) turns them into the
        // float scale, exactly as KQuantDeInterleave emits d_bytes for Fp16Pack.
        Func scale_bytes("uninterleave_scale_bytes");
        scale_bytes(byte, blk, j, x) = blocks(scale_stride * j + byte, blk, x);
        return {{codes, scale_bytes}, {}};
    }

private:
    int n_cols_, blocklen_, block_size_;
    RepackWeightCode code_kind_;
    RepackWeightScale scale_kind_;
};

// Weight-decode scheme for a "simple" repack weight family (Q4_0/Q8_0/IQ4_NL/
// MXFP4): un-interleave -> [codebook] -> one-level scale. The col dims ride the
// dimension-general ScaleDequant/Codebook via Halide::_. SeveredEncode (3-D)
// stands in for the pre-quantized weight buffer, severed by the gemv/gemm
// generator's compute_offline.
inline std::unique_ptr<Halide::Approximation> make_repack_weight_scheme(
    int n_cols, int blocklen, int block_bytes, RepackWeightCode code_kind, RepackWeightScale scale_kind,
    Halide::Buffer<int8_t> table = {}, int block_size = 32) {
    using namespace Halide;
    UnInterleaveWeight unpack{n_cols, blocklen, block_size, code_kind, scale_kind};
    // Interpret the scale-header bytes UnInterleaveWeight gathers with the same
    // packs the plain codecs use -- no per-kind scale decode duplicated here.
    std::unique_ptr<Approximation> scale_pack =
        scale_kind == RepackWeightScale::Fp16 ? std::unique_ptr<Approximation>(std::make_unique<Fp16Pack>()) : scale_kind == RepackWeightScale::F32 ? std::unique_ptr<Approximation>(std::make_unique<F32Pack>()) :
                                                                                                                                                      std::unique_ptr<Approximation>(std::make_unique<E8M0Pack>());
    if (code_kind == RepackWeightCode::RawNibble) {
        return std::make_unique<TrustedInverse>(
            SeveredEncode{block_bytes, 3},
            Compose{std::move(unpack), Apply{0, 1, 1, Codebook{table}},
                    Apply{1, 1, 1, std::move(scale_pack)}, ScaleDequant{block_size, 1}});
    }
    return std::make_unique<TrustedInverse>(
        SeveredEncode{block_bytes, 3},
        Compose{std::move(unpack), Apply{1, 1, 1, std::move(scale_pack)}, ScaleDequant{block_size, 1}});
}

// K-quant repack weight decode (block_q{4,5,6,2}_Kx8, n_cols=8): a bespoke,
// verified-unchanged transcription of the repack_gemv_generators.cpp
// weight_value helpers -- the interleaved 256-element super-block with a
// two-level scale (and, for Q4_K/Q5_K, get_scale_min_k4 with the column index
// standing in for the sub-block index). Its decode reads the severed 3-D
// weight buffer and emits Wt(kk, blk, j, x) directly (kept as one leaf rather
// than composed from TwoLevelScaleDequant + the scale-min packs, because the
// interleave is intricate enough that a direct transcription is far less
// error-prone; the SeveredEncode encoder half is severed by the matmul
// generator's compute_offline). blocklen is the interleave width (4 or 8).
enum class KQuantWeightFamily { Q4_K,
                                Q5_K,
                                Q6_K,
                                Q2_K };

// The genuinely repack-specific half of a K-quant weight decode: the *byte
// addressing* that maps a logical (element kk, sub-block, column j, col-group
// x) back to the interleaved block's scattered qs/qh/ql bytes and its scale
// region. It is the K-quant analog of UnInterleaveWeight -- pure permutation,
// no dequant arithmetic. It emits the same logical field slots the plain
// K-quant decode's StructPack+packs produce, so the shared downstream
// Compose (Fp16Pack on the fp16 headers, then TwoLevelScaleDequant, in
// LOGICAL element order) finishes the job identically:
//
//   Q4_K/Q5_K: {d_bytes, dmin_bytes, scale_min, codes}   sub_size 32
//   Q6_K:      {d_bytes,             scale,     codes}   sub_size 16 (no min)
//   Q2_K:      {d_bytes, dmin_bytes, scale_min, codes}   sub_size 16
//
// (has-min families emit scale and min combined in one scale_min(plane, sub, ...)
// field, plane 0 = scale / 1 = min -- the same shape K4ScaleMinPack and
// PlanarBitPack's plane-axis mode produce, so TwoLevelScaleDequant consumes it
// identically.) scale/min are produced as VALUES here (not bytes) because their
// bit layouts can't be delegated to the plain packs: get_scale_min_k4 (Q4_K/Q5_K)
// does its bit-math on what is the *column* index j in the repack while the
// sub-block rides a separate axis -- an axis transpose K4ScaleMinPack's
// (sub-first) interface can't express -- and the qs/qh code stream is
// column-interleaved, so PlanarBitPack's contiguous-window assumption doesn't
// hold. The scale index reduces to kk/sub_size in logical order for all four families
// (verified: Q6_K's base_l/base_h and Q2_K's sm_idx both collapse to kk/16
// since blocklen divides 16), which is exactly what TwoLevelScaleDequant
// consumes, so no re-order is needed downstream.
class KQuantDeInterleave : public Halide::Approximation {
public:
    KQuantDeInterleave(KQuantWeightFamily family, int blocklen)
        : family_(family), blocklen_(blocklen) {
    }

    Halide::EncodeResult encode(std::vector<Halide::Func>) override {
        _halide_user_error << "KQuantDeInterleave is decode-only (SeveredEncode is the severed encoder).\n";
        return {};
    }

    Halide::DecodeResult decode(std::vector<Halide::Func> encoded) override {
        using namespace Halide;
        Func b = encoded[0];  // blocks(byte, k-superblock, col-group)
        Var byte("byte"), kk("kk"), blk("blk"), sub("sub"), plane("plane"), j("j"), x("x");
        const int bl = blocklen_;
        const int nc = 8;

        // has-min families (Q4_K/Q5_K/Q2_K) emit scale and min combined in one
        // field scale_min(plane, sub, ...) (plane 0 = scale, 1 = min), the shape
        // TwoLevelScaleDequant consumes; Q6_K (no min) emits a plain scale.
        Func codes("kq_codes"), scale("kq_scale"), scale_min("kq_scale_min"), d_bytes("kq_d"), dmin_bytes("kq_dmin");

        if (family_ == KQuantWeightFamily::Q4_K || family_ == KQuantWeightFamily::Q5_K) {
            const bool is_q5 = family_ == KQuantWeightFamily::Q5_K;
            const int kScalesOffset = 32;
            const int kQsOffset = is_q5 ? 384 : 128;
            const int kQhOffset = 128;  // Q5_K only

            Expr iter = kk / 64, local64 = kk % 64, half64 = local64 / 32, lpos = local64 % 32;
            Expr k_inner = lpos / bl, ii = lpos % bl, k = iter * (32 / bl) + k_inner;
            Expr qs_byte = b(kQsOffset + k * nc * bl + j * bl + ii, blk, x);
            Expr nibble = cast<int32_t>(select(half64 == 0, qs_byte & 0x0f, qs_byte >> 4));
            Expr value = nibble;
            if (is_q5) {
                Expr qh_byte = b(kQhOffset + k_inner * (bl * nc) + j * bl + ii, blk, x);
                Expr h_bit = cast<int32_t>((cast<uint32_t>(qh_byte) >> (iter * 2 + half64)) & 1);
                value = nibble | (h_bit << 4);
            }
            codes(kk, blk, j, x) = value;

            // get_scale_min_k4, addressed by window(sub) and bit-position j,
            // scale and min combined into one (plane, sub) field.
            Expr window = (sub / 4) * 48 + (sub % 4) * 12;
            Expr jj = clamp(j - 4, 0, 3);
            Expr sc = select(j < 4, b(kScalesOffset + window + j, blk, x) & 0x3f,
                             cast<uint8_t>((b(kScalesOffset + window + 8 + jj, blk, x) & 0x0f) |
                                           ((b(kScalesOffset + window + jj, blk, x) >> 6) << 4)));
            Expr mn = select(j < 4, b(kScalesOffset + window + j + 4, blk, x) & 0x3f,
                             cast<uint8_t>((b(kScalesOffset + window + 8 + jj, blk, x) >> 4) |
                                           ((b(kScalesOffset + window + 4 + jj, blk, x) >> 6) << 4)));
            scale_min(plane, sub, blk, j, x) = cast<uint8_t>(select(plane == 0, sc, mn));

            d_bytes(byte, blk, j, x) = b(2 * j + byte, blk, x);
            dmin_bytes(byte, blk, j, x) = b(16 + 2 * j + byte, blk, x);
            return {{d_bytes, dmin_bytes, scale_min, codes}, {}};
        } else if (family_ == KQuantWeightFamily::Q6_K) {
            const int kScalesOffset = 16, kQlOffset = 144, kQhOffset = 1168;
            const int blocks_per_half = 64 / bl;
            constexpr int kQlSize = (256 * 8) / 2, kQhSize = (256 * 8) / 4;

            Expr local128 = kk % 128, is_high = local128 >= 64, pos64 = local128 % 64, i = pos64 % bl;
            Expr base_l = kk - i - select(is_high, 64, 0), base_h = base_l + 64;
            Expr k = (base_l / 128) * blocks_per_half + (base_l % 128) / bl;
            Expr ql_byte = b(kQlOffset + clamp(k * nc * bl + j * bl + i, 0, kQlSize - 1), blk, x);
            Expr nibble = cast<int32_t>(select(is_high, ql_byte >> 4, ql_byte & 0x0f));
            Expr qh_shift = select(is_high, ((base_h % 128) / 32) * 2, ((base_l % 128) / 32) * 2);
            Expr qh_idx_l = (base_l / 128) * 32 + ((base_l + i) % 32);
            Expr qh_idx_h = (base_h / 128) * 32 + ((base_h + i) % 32);
            Expr qh_off_l = clamp((qh_idx_l / bl) * (bl * nc) + j * bl + (qh_idx_l % bl), 0, kQhSize - 1);
            Expr qh_off_h = clamp((qh_idx_h / bl) * (bl * nc) + j * bl + (qh_idx_h % bl), 0, kQhSize - 1);
            Expr qh_byte = b(kQhOffset + select(is_high, qh_off_h, qh_off_l), blk, x);
            Expr hi2 = cast<int32_t>((qh_byte >> qh_shift) & 3);
            codes(kk, blk, j, x) = (nibble | (hi2 << 4)) - 32;

            // 16 plain signed int8 scales, sub = kk/16 (see class comment).
            scale(sub, blk, j, x) = cast<int32_t>(reinterpret<int8_t>(b(kScalesOffset + sub * nc + j, blk, x)));
            d_bytes(byte, blk, j, x) = b(2 * j + byte, blk, x);
            return {{d_bytes, scale, codes}, {}};
        } else {  // Q2_K (blocklen fixed 8 upstream)
            const int kDminOffset = 16, kScalesOffset = 32, kQsOffset = 160;
            Expr half = kk / 128, local = kk % 128, subg = local / 32, rem32 = local % 32;
            Expr k = half * 4 + rem32 / bl, i = rem32 % bl;
            Expr qs_byte = b(kQsOffset + k * nc * bl + j * bl + i, blk, x);
            codes(kk, blk, j, x) = cast<int32_t>((qs_byte >> (subg * 2)) & 3);

            // scale/min nibble pair, sub = kk/16 (see class comment), combined
            // into one (plane, sub) field (plane 0 = scale, 1 = min).
            Expr sm_idx = (sub / 8) * 64 + ((sub % 8) / 2) * 16 + j * 2 + (sub % 2);
            Expr sm_byte = b(kScalesOffset + sm_idx, blk, x);
            scale_min(plane, sub, blk, j, x) =
                select(plane == 0, cast<int32_t>(sm_byte & 0x0f), cast<int32_t>(sm_byte >> 4));
            d_bytes(byte, blk, j, x) = b(2 * j + byte, blk, x);
            dmin_bytes(byte, blk, j, x) = b(kDminOffset + 2 * j + byte, blk, x);
            return {{d_bytes, dmin_bytes, scale_min, codes}, {}};
        }
    }

private:
    KQuantWeightFamily family_;
    int blocklen_;
};

// K-quant repack weight scheme: the de-interleave addressing leaf composed
// with the SAME arithmetic pieces the plain K-quant decode uses (Fp16Pack for
// the fp16 headers, then TwoLevelScaleDequant in logical element order),
// paired with a severed 3-D encoder (the weight is pre-quantized; encode is
// severed by compute_offline). No BlockReshape: the weight stays block-indexed
// (kk, blk) with the column dims (j, x) riding through.
inline std::unique_ptr<Halide::Approximation> make_kquant_repack_weight_scheme(
    KQuantWeightFamily family, int blocklen, int block_bytes) {
    using namespace Halide;
    const bool has_min = family != KQuantWeightFamily::Q6_K;
    const int sub_size = family == KQuantWeightFamily::Q4_K || family == KQuantWeightFamily::Q5_K ? 32 : 16;
    Compose decode =
        has_min ? Compose{KQuantDeInterleave{family, blocklen},
                          Apply{0, 1, 1, Fp16Pack{}},  // d_bytes -> d
                          Apply{1, 1, 1, Fp16Pack{}},  // dmin_bytes -> dmin
                          TwoLevelScaleDequant{sub_size, true}} :
                  Compose{KQuantDeInterleave{family, blocklen},
                          Apply{0, 1, 1, Fp16Pack{}},  // d_bytes -> d
                          TwoLevelScaleDequant{sub_size, false}};
    return std::make_unique<TrustedInverse>(SeveredEncode{block_bytes, 3}, std::move(decode));
}

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

struct CodePackField {
    std::unique_ptr<Halide::Approximation> pack;
    int bytes;
};

inline CodePackField make_code_pack(int block_size, int code_bits, int qmax, bool one_bit_is_bitpack = true) {
    using namespace Halide;
    if (code_bits == 4) {
        return {std::make_unique<PlanarBitPack>(4, 2, block_size / 2, qmax), block_size / 2};
    }
    if (one_bit_is_bitpack && code_bits == 1) {
        return {std::make_unique<BitPack>(), block_size / 8};
    }
    return {std::make_unique<BytePack>(), block_size};
}

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
    CodePackField code = make_code_pack(block_size, code_bits, qmax);
    return std::make_unique<Compose>(
        StructPack{{2, code.bytes}, {1, 0}},
        Apply{1, 1, 1, Fp16Pack{}},
        Apply{0, 1, 1, std::move(code.pack)},
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

// quantize -> pack codes -> pack scale -> pack min -> concatenate (scale, min,
// codes; matching block_q4_1/block_q5_1's `{fp16 d; fp16 m; ...qs;}` layout)
// -> reshape -- the affine (min+scale) counterpart to
// make_symmetric_block_scheme(), used by Q4_1 (code_bits=4, ClampedInt8). Q5_1
// pairs with make_affine_5bit_block_scheme() below instead, since it also
// needs the qh high-bit field.
inline std::unique_ptr<Halide::Approximation> make_affine_block_scheme(
    int block_size, int levels, AffineRounding rounding, int code_bits, bool block_indexed = false) {
    using namespace Halide;
    CodePackField code = make_code_pack(block_size, code_bits, /*qmax=*/0, /*one_bit_is_bitpack=*/false);
    return std::make_unique<Compose>(
        StructPack{{2, 2, code.bytes}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},            // pack min
        Apply{1, 1, 1, Fp16Pack{}},            // pack scale
        Apply{0, 1, 1, std::move(code.pack)},  // pack codes
        AffineQuantize{block_size, levels, rounding},
        BlockReshape{block_size, block_indexed});
}
inline std::unique_ptr<Halide::Approximation> make_affine_block_codec(
    int block_size, int levels, AffineRounding rounding, int code_bits) {
    return make_affine_block_scheme(block_size, levels, rounding, code_bits, /*block_indexed=*/true);
}

// Symmetric quantize (like make_symmetric_block_scheme()) but 5-bit: the
// extra 5th bit goes through FiveBitPack instead of PlanarBitPack, producing
// an extra `qh` field between the scale and the nibble bytes in the output
// layout (matching block_q5_0's `{fp16 d; qh[4]; qs[16];}`). `qmax` is
// always 16 (5-bit signed range [-16, 15]).
inline std::unique_ptr<Halide::Approximation> make_symmetric_5bit_block_scheme(int block_size, int qmax,
                                                                               bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 4, block_size / 2}, {2, 1, 0}},
        Apply{2, 1, 1, Fp16Pack{}},
        Apply{0, 1, 2, FiveBitPack{block_size, qmax}},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::TruncateHalfUpWithOffset, ScaleAnchor::ExtremeSignedValue},
        BlockReshape{block_size, block_indexed});
}
inline std::unique_ptr<Halide::Approximation> make_symmetric_5bit_block_codec(int block_size, int qmax) {
    return make_symmetric_5bit_block_scheme(block_size, qmax, /*block_indexed=*/true);
}

// Affine quantize (like make_affine_block_scheme()) but 5-bit: same
// FiveBitPack addition as make_symmetric_5bit_block_scheme(), plus the min
// field affine quantization needs, matching block_q5_1's
// `{fp16 d; fp16 m; qh[4]; qs[16];}`. `qmax=0` when calling FiveBitPack
// here (unlike Q5_0's 16): AffineQuantize's codes are already unsigned
// [0, levels], not centered, so there's no offset to re-apply before
// splitting into nibble+high-bit.
inline std::unique_ptr<Halide::Approximation> make_affine_5bit_block_scheme(int block_size, int levels,
                                                                            AffineRounding rounding,
                                                                            bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 2, 4, block_size / 2}, {2, 3, 1, 0}},
        Apply{3, 1, 1, Fp16Pack{}},                           // pack min
        Apply{2, 1, 1, Fp16Pack{}},                           // pack scale
        Apply{0, 1, 2, FiveBitPack{block_size, /*qmax=*/0}},  // pack codes
        AffineQuantize{block_size, levels, rounding},
        BlockReshape{block_size, block_indexed});
}
inline std::unique_ptr<Halide::Approximation> make_affine_5bit_block_codec(int block_size, int levels,
                                                                           AffineRounding rounding) {
    return make_affine_5bit_block_scheme(block_size, levels, rounding, /*block_indexed=*/true);
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
inline std::unique_ptr<Halide::Approximation> make_symmetric_byte_sum_block_scheme(int block_size, int qmax,
                                                                                   bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{2, 2, block_size}, {1, 2, 0}},
        Apply{2, 1, 1, Fp16Pack{}},  // pack sum
        Apply{1, 1, 1, Fp16Pack{}},  // pack scale
        Apply{0, 1, 1, BytePack{}},  // pack codes
        AppendCodeSum{block_size},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::Nearest, ScaleAnchor::AbsMax},
        BlockReshape{block_size, block_indexed});
}

// Block-indexed (kk, blk) codec sibling of make_symmetric_byte_sum_block_scheme,
// for Q8_1 as a vec_dot activation. The derived `s` field is dropped by
// AppendCodeSum::decode, so decode yields a plain d*code dequant.
inline std::unique_ptr<Halide::Approximation> make_symmetric_byte_sum_block_codec(int block_size, int qmax) {
    return make_symmetric_byte_sum_block_scheme(block_size, qmax, /*block_indexed=*/true);
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
inline std::unique_ptr<Halide::Approximation> make_q8_k_scheme(int block_size, int qmax,
                                                               bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{{4, block_size, (block_size / 16) * 2}, {1, 0, 2}},
        Apply{2, 1, 1, Int16Pack{}},  // pack bsums
        Apply{1, 1, 1, F32Pack{}},    // pack scale
        Apply{0, 1, 1, BytePack{}},   // pack codes
        AppendGroupSumsInt16{16},
        SymmetricAffineQuantize{block_size, qmax, RoundingMode::NearestEvenClampedHigh,
                                ScaleAnchor::ExtremeSignedValueTwoStep},
        BlockReshape{block_size, block_indexed});
}

// Block-indexed (kk, blk) codec sibling of make_q8_k_scheme, for Q8_K as a
// vec_dot activation. The derived `bsums` field is dropped by
// AppendGroupSumsInt16::decode, so decode yields a plain d*code dequant.
inline std::unique_ptr<Halide::Approximation> make_q8_k_codec(int block_size, int qmax) {
    return make_q8_k_scheme(block_size, qmax, /*block_indexed=*/true);
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
    int num_scales, bool scale_first, bool block_indexed = false) {
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
            BlockReshape{block_size, block_indexed},
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
    std::unique_ptr<Halide::Approximation> code_pack, bool block_indexed = false) {
    using namespace Halide;
    Compose decode = has_min ? Compose{
                                   StructPack{field_widths, input_index},
                                   Apply{0, 1, 1, Fp16Pack{}},                 // d
                                   Apply{1, 1, 1, Fp16Pack{}},                 // dmin
                                   Apply{2, 1, 1, std::move(scale_min_pack)},  // scale_min bytes -> scale_min(plane, sub)
                                   Apply{3, 1, 1, std::move(code_pack)},       // codes
                                   TwoLevelScaleDequant{sub_size, true},
                                   BlockReshape{block_size, block_indexed},
                               } :
                               Compose{
                                   StructPack{field_widths, input_index},
                                   Apply{0, 1, 1, Fp16Pack{}},                 // d
                                   Apply{1, 1, 1, std::move(scale_min_pack)},  // scale
                                   Apply{2, 1, 1, std::move(code_pack)},       // codes
                                   TwoLevelScaleDequant{sub_size, false},
                                   BlockReshape{block_size, block_indexed},
                               };
    return std::make_unique<TrustedInverse>(ExternQuantize{std::move(extern_name)}, std::move(decode));
}

inline std::unique_ptr<Halide::Approximation> make_combined_bit_codec(
    std::vector<int> field_widths, std::vector<int> input_index,
    std::unique_ptr<Halide::Approximation> low_pack,
    std::unique_ptr<Halide::Approximation> high_pack,
    int high_weight, int offset) {
    using namespace Halide;
    return std::make_unique<Compose>(
        StructPack{std::move(field_widths), std::move(input_index)},
        Apply{0, 1, 1, std::move(low_pack)},
        Apply{1, 1, 1, std::move(high_pack)},
        CombineBits{high_weight, offset});
}

// IQ grid formats (IQ2_S/IQ3_XXS/IQ3_S): a bespoke grid+sign+scale decode leaf
// that emits values in the superblock's nested structure, with
// BlockReshape(`block_extents`) doing the flat<->block reshape.
inline std::unique_ptr<Halide::Approximation> make_grid_scheme(
    std::string extern_name, std::unique_ptr<Halide::Approximation> grid_leaf,
    std::vector<int> block_extents, bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<TrustedInverse>(
        ExternQuantize{std::move(extern_name)},
        Compose{std::move(grid_leaf), BlockReshape{std::move(block_extents), block_indexed}});
}

inline std::unique_ptr<Halide::Approximation> make_severed_grid_scheme(
    int block_bytes, std::unique_ptr<Halide::Approximation> grid_leaf,
    std::vector<int> block_extents, bool block_indexed = false) {
    using namespace Halide;
    return std::make_unique<TrustedInverse>(
        SeveredEncode{block_bytes},
        Compose{std::move(grid_leaf), BlockReshape{std::move(block_extents), block_indexed}});
}

// IQ4_NL: 32-element blocks, 4-bit codes into a 16-value non-uniform
// codebook, one fp16 scale per block -- {fp16 d; qs[16];}, 18 bytes. Extern
// quantize; decode unpacks {code_bytes, scale_bytes} (ScaleFirst), looks the
// nibbles up in the codebook, applies the one fp16 scale, and reshapes to a
// flat row.
inline std::unique_ptr<Halide::Approximation> make_iq4_nl_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_iq4nl");
    return make_codebook_scheme("iq4_nl_quantize_via_ggml", 32, table,
                                std::make_unique<PlanarBitPack>(4, 2, 16, 0), 16,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/true, block_indexed);
}

// Block-indexed (kk, blk) codec sibling of make_iq4_nl_scheme, for vec_dot.
inline std::unique_ptr<Halide::Approximation> make_iq4_nl_codec() {
    return make_iq4_nl_scheme(/*block_indexed=*/true);
}

// MXFP4: 32-element blocks, 4-bit codes into the same-shaped 16-value
// codebook as IQ4_NL (different values), one E8M0 (1-byte, power-of-two)
// scale per block -- {e8m0 e; qs[16];}, 17 bytes.
inline std::unique_ptr<Halide::Approximation> make_mxfp4_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_mxfp4");
    return make_codebook_scheme("mxfp4_quantize_via_ggml", 32, table,
                                std::make_unique<PlanarBitPack>(4, 2, 16, 0), 16,
                                std::make_unique<E8M0Pack>(), 1, 1, /*scale_first=*/true, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_mxfp4_codec() {
    return make_mxfp4_scheme(/*block_indexed=*/true);
}

// TQ2_0: 256-element superblock, 2-bit codes (each in {0,1,2}, meaning
// {-1,0,1}) via PlanarBitPack(2, 4, 32, 0)'s window-interleaved layout, one
// fp16 scale -- {qs[64]; fp16 d;}, 66 bytes -- qs *before* d, unlike most
// formats here (StructPack's codes-first field order below).
inline std::unique_ptr<Halide::Approximation> make_tq2_0_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_tq2_0");
    return make_codebook_scheme("tq2_0_quantize_via_ggml", 256, table,
                                std::make_unique<PlanarBitPack>(2, 4, 32, 0), 64,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/false, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_tq2_0_codec() {
    return make_tq2_0_scheme(/*block_indexed=*/true);
}

// TQ1_0: 256-element superblock, base-3 codes (each in {0,1,2}, meaning
// {-1,0,1}) via TritPack's 5-trits/byte (+4-trits/byte tail) packing, one
// fp16 scale -- {qs[48]; qh[4]; fp16 d;}, 54 bytes -- qs+qh (combined, 52
// bytes) *before* d, like TQ2_0. Reuses TQ2_0's exact {-1, 0, 1, unused}
// codebook (TritPack's codes are the same raw 0/1/2 digit either way).
inline std::unique_ptr<Halide::Approximation> make_tq1_0_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[4] = {-1, 0, 1, 0};  // index 3 is never produced
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_tq1_0");
    return make_codebook_scheme("tq1_0_quantize_via_ggml", 256, table,
                                std::make_unique<TritPack>(), 52,
                                std::make_unique<Fp16Pack>(), 2, 1, /*scale_first=*/false, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_tq1_0_codec() {
    return make_tq1_0_scheme(/*block_indexed=*/true);
}

// NVFP4: 64-element block, 4 sub-blocks of 16 elements each, 4-bit codes
// into the same 16-value codebook MXFP4 uses (NVFP4 is MXFP4 with
// finer-grained scales), one UE4M3 scale *per sub-block* via UE4M3Pack --
// {d[4]; qs[32];}, 36 bytes -- ScaleDequant's num_scales=4 (not 1) is what
// makes each sub-block's dequantize use its own scale byte instead of one
// shared scale for the whole 64-element block.
inline std::unique_ptr<Halide::Approximation> make_nvfp4_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_nvfp4");
    return make_codebook_scheme("nvfp4_quantize_via_ggml", 64, table,
                                std::make_unique<PlanarBitPack>(4, 2, 8, 0), 32,
                                std::make_unique<UE4M3Pack>(), 4, /*num_scales=*/4, /*scale_first=*/true, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_nvfp4_codec() {
    return make_nvfp4_scheme(/*block_indexed=*/true);
}

// Q4_K: 256-element superblock, 8 sub-blocks of 32 elements each, plain
// 4-bit codes (PlanarBitPack(4, 2, 32, 0)) and get_scale_min_k4-packed
// (scale, min) pairs (K4ScaleMinPack) -- {fp16 d; fp16 dmin; scales[12];
// qs[128];}, 144 bytes, fields already in {d, dmin, scale_min, code} logical
// order. Two-level scale (d*scale(sub)*code - dmin*min(sub)).
inline std::unique_ptr<Halide::Approximation> make_q4_k_scheme(bool block_indexed = false) {
    using namespace Halide;
    return make_k_quant_scheme("q4_k_quantize_via_ggml", 256, 32, {2, 2, 12, 128}, {0, 1, 2, 3}, true,
                               std::make_unique<K4ScaleMinPack>(),
                               std::make_unique<PlanarBitPack>(4, 2, 32, 0), block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_q4_k_codec() {
    return make_q4_k_scheme(/*block_indexed=*/true);
}

// Q5_K: same super-block/sub-block/scale-min shape as Q4_K, but each code
// is 5 bits: a plain 4-bit low nibble (PlanarBitPack(4, 2, 32, 0)) plus a
// 5th high bit from a separate 32-byte, 8-window rotating-bit array
// (PlanarBitPack(1, 8, 32, 0)) -- {fp16 d; fp16 dmin; scales[12]; qh[32];
// qs[128];}, 176 bytes. qh+qs are adjacent in memory, treated as one
// 160-byte combined "code" field, split by an inner Compose {StructPack ->
// unpack qh/qs -> CombineBits} (HighFirst: qh before qs), offset=0 since
// Q5_K's code is a plain 0..31 unsigned magnitude, not recentered.
inline std::unique_ptr<Halide::Approximation> make_q5_k_scheme(bool block_indexed = false) {
    using namespace Halide;
    return make_k_quant_scheme(
        "q5_k_quantize_via_ggml", 256, 32, {2, 2, 12, 160}, {0, 1, 2, 3}, true,
        std::make_unique<K4ScaleMinPack>(),
        // Combined 5-bit code: qh (high bit) + qs (low nibble), split by an
        // inner Compose. HighFirst (qh before qs); offset 0 (plain 0..31).
        make_combined_bit_codec(
            {32, 128}, {1, 0},                             // {qh[32]; qs[128];} -> {low, high}
            std::make_unique<PlanarBitPack>(4, 2, 32, 0),  // qs -> low nibble
            std::make_unique<PlanarBitPack>(1, 8, 32, 0),  // qh -> high bit
            16, 0),
        block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_q5_k_codec() {
    return make_q5_k_scheme(/*block_indexed=*/true);
}

// Q2_K: 256-element superblock, 16 sub-blocks of 16 elements each, plain
// 2-bit codes (PlanarBitPack(2, 4, 32, 0)) and independent per-sub-block
// nibble-pair (scale, min) via PlanarBitPack's plane-axis mode (low nibble =
// scale = plane 0, high nibble = min = plane 1; no bit-interleaving across
// sub-blocks) -- {scales[16]; qs[64]; fp16 d; fp16 dmin;}, 84 bytes, fields
// on-disk in {scale_min, code, d, dmin} order (scale_min/code *before* d/dmin,
// unlike Q4_K/Q5_K); StructPack's input_index normalizes back to {d, dmin,
// scale_min, code}.
inline std::unique_ptr<Halide::Approximation> make_q2_k_scheme(bool block_indexed = false) {
    using namespace Halide;
    return make_k_quant_scheme("q2_k_quantize_via_ggml", 256, 16, {16, 64, 2, 2}, {2, 3, 0, 1}, true,
                               std::make_unique<PlanarBitPack>(4, 2, 16, 0, /*plane_axis=*/true),
                               std::make_unique<PlanarBitPack>(2, 4, 32, 0), block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_q2_k_codec() {
    return make_q2_k_scheme(/*block_indexed=*/true);
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
inline std::unique_ptr<Halide::Approximation> make_q3_k_scheme(bool block_indexed = false) {
    using namespace Halide;
    return make_k_quant_scheme(
        "q3_k_quantize_via_ggml", 256, 16, {96, 12, 2}, {2, 1, 0}, false,
        std::make_unique<Q3KScalePack>(),
        // Combined 3-bit code: hmask (high bit) + qs (low 2 bits). HighFirst;
        // offset 4 (recenters to signed [-4, 3]).
        make_combined_bit_codec(
            {32, 64}, {1, 0},                              // {hmask[32]; qs[64];} -> {low, high}
            std::make_unique<PlanarBitPack>(2, 4, 32, 0),  // qs -> low 2 bits
            std::make_unique<PlanarBitPack>(1, 8, 32, 0),  // hmask -> high bit
            4, 4),
        block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_q3_k_codec() {
    return make_q3_k_scheme(/*block_indexed=*/true);
}

// Q6_K: 256-element superblock, 16 sub-blocks of 16 elements each, no min --
// each code is 6 bits: a plain 4-bit low nibble over *two* 128-element
// halves (PlanarBitPack(4, 2, 64, 0)) plus 2 high bits
// (PlanarBitPack(2, 4, 32, 0)), recentered by -32; scale is 16 plain SIGNED
// int8 values, no bit-interleaving at all (BytePack -- its plain
// reinterpret<int8_t> is exactly what this needs) -- {ql[128]; qh[64];
// scales[16]; fp16 d;}, 210 bytes. ql+qh are adjacent in memory, treated as
// one 192-byte combined "code" field (LowFirst: ql before qh).
inline std::unique_ptr<Halide::Approximation> make_q6_k_scheme(bool block_indexed = false) {
    using namespace Halide;
    return make_k_quant_scheme(
        "q6_k_quantize_via_ggml", 256, 16, {192, 16, 2}, {2, 1, 0}, false,
        std::make_unique<BytePack>(),  // 16 signed int8 scales
        // Combined 6-bit code: ql (low nibble) + qh (high 2 bits). LowFirst;
        // offset 32 (recenters to signed [-32, 31]).
        make_combined_bit_codec(
            {128, 64}, {0, 1},                             // {ql[128]; qh[64];} -> {low, high}
            std::make_unique<PlanarBitPack>(4, 2, 64, 0),  // ql -> low nibble
            std::make_unique<PlanarBitPack>(2, 4, 32, 0),  // qh -> high 2 bits
            16, 32),
        block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_q6_k_codec() {
    return make_q6_k_scheme(/*block_indexed=*/true);
}

// IQ2_S/IQ3_XXS/IQ3_S: see IQ2SGridDequantize/IQ3XXSGridDequantize/
// IQ3SGridDequantize above for the bit-layout rationale -- each is a bespoke,
// self-contained grid+sign+scale decode leaf (its three bit layouts share no
// sub-formula worth abstracting). Extern quantize; the grid leaf's decode
// produces block-indexed values, and BlockReshape composes the flat<->block
// reshape on top.
inline std::unique_ptr<Halide::Approximation> make_iq2_s_scheme(bool block_indexed = false) {
    return make_grid_scheme("iq2_s_quantize_via_ggml", std::make_unique<IQ2SGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq2_s_codec() {
    return make_iq2_s_scheme(/*block_indexed=*/true);
}

inline std::unique_ptr<Halide::Approximation> make_iq3_xxs_scheme(bool block_indexed = false) {
    return make_grid_scheme("iq3_xxs_quantize_via_ggml", std::make_unique<IQ3XXSGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq3_xxs_codec() {
    return make_iq3_xxs_scheme(/*block_indexed=*/true);
}

inline std::unique_ptr<Halide::Approximation> make_iq3_s_scheme(bool block_indexed = false) {
    return make_grid_scheme("iq3_s_quantize_via_ggml", std::make_unique<IQ3SGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq3_s_codec() {
    return make_iq3_s_scheme(/*block_indexed=*/true);
}

// IQ2_XS/IQ2_XXS/IQ1_S/IQ1_M: importance-matrix-only formats with no forward
// map -- SeveredEncode stands in for the (always-severed) encode half so the
// dequantize/vec_dot still go through approximate_by/compute_offline. Block
// bytes: 74 / 66 / 50 / 56.
inline std::unique_ptr<Halide::Approximation> make_iq2_xs_scheme(bool block_indexed = false) {
    return make_severed_grid_scheme(74, std::make_unique<IQ2XSGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq2_xs_codec() {
    return make_iq2_xs_scheme(/*block_indexed=*/true);
}

inline std::unique_ptr<Halide::Approximation> make_iq2_xxs_scheme(bool block_indexed = false) {
    return make_severed_grid_scheme(66, std::make_unique<IQ2XXSGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq2_xxs_codec() {
    return make_iq2_xxs_scheme(/*block_indexed=*/true);
}

inline std::unique_ptr<Halide::Approximation> make_iq1_s_scheme(bool block_indexed = false) {
    return make_severed_grid_scheme(50, std::make_unique<IQ1SGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq1_s_codec() {
    return make_iq1_s_scheme(/*block_indexed=*/true);
}

inline std::unique_ptr<Halide::Approximation> make_iq1_m_scheme(bool block_indexed = false) {
    return make_severed_grid_scheme(56, std::make_unique<IQ1MGridDequantize>(), {8, 4, 8}, block_indexed);
}
inline std::unique_ptr<Halide::Approximation> make_iq1_m_codec() {
    return make_iq1_m_scheme(/*block_indexed=*/true);
}

// IQ4_XS: 256-element superblock, 8 sub-blocks of 32 elements, the superblock
// generalization of IQ4_NL's fixed 16-value codebook -- plain 4-bit codes
// (PlanarBitPack(4, 2, 16, 0)) into the same kvalues_iq4nl table, scaled by
// `d * (ls - 32)`, a two-level scale (no min) whose per-sub-block `ls` is
// bit-interleaved across two byte fields (IQ4XSScalePack) -- {fp16 d;
// scales_h[2]; scales_l[4]; qs[128];}, 136 bytes.
inline std::unique_ptr<Halide::Approximation> make_iq4_xs_scheme(bool block_indexed = false) {
    using namespace Halide;
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> table = make_static_codebook(kValues, "kvalues_iq4nl_xs");
    return std::make_unique<TrustedInverse>(
        ExternQuantize{"iq4_xs_quantize_via_ggml"},
        Compose{
            StructPack{{2, 2, 4, 128}, {0, 1, 2, 3}},    // -> {d, scales_h, scales_l, qs}
            Apply{0, 1, 1, Fp16Pack{}},                  // d
            Apply{1, 1, 2, IQ4XSScalePack{}},            // {scales_h, scales_l} -> scale(sub)
            Apply{2, 1, 1, PlanarBitPack{4, 2, 16, 0}},  // qs -> nibbles
            Apply{2, 1, 1, Codebook{table}},             // nibbles -> codebook values
            TwoLevelScaleDequant{32, false},
            BlockReshape{256, block_indexed},
        });
}
inline std::unique_ptr<Halide::Approximation> make_iq4_xs_codec() {
    return make_iq4_xs_scheme(/*block_indexed=*/true);
}

}  // namespace ggml_halide
