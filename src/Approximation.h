#ifndef HALIDE_APPROXIMATION_H
#define HALIDE_APPROXIMATION_H

/** \file
 * Defines Approximation, a core interface for lossy, quantified
 * Func-to-Func transformations (e.g. a quantize/dequantize round trip), and
 * Compose/Apply, which build larger Approximations out of smaller ones. See
 * Func::approximate_by(), which splices such a round trip into an existing
 * call graph, and doc/ApproximationDesign.md for the design rationale.
 */

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "Func.h"

namespace Halide {

/** The result of Approximation::encode(): the Func(s) that make up the
 * signature contract other code is expected to consume, plus any extra
 * intermediate Funcs ("handles") that have no meaning outside scheduling
 * (e.g. per-block reduction Funcs) but must still be scheduled by whoever
 * calls encode(). */
struct EncodeResult {
    std::vector<Func> encoded;
    std::vector<Func> handles;
};

/** The result of Approximation::decode(): decoded is the round-trip
 * replacement for whatever Func(s) were originally encoded, plus any
 * additional scheduling-only handles. When an Approximation is used
 * directly with Func::approximate_by(), decoded must contain exactly one
 * Func; when it's used as one stage of a larger Compose/Apply chain,
 * decoded may contain however many Funcs the next stage down expects. */
struct DecodeResult {
    std::vector<Func> decoded;
    std::vector<Func> handles;
};

/** Approximation is the base class for a lossy, quantified transformation
 * of one or more Funcs' values -- e.g. quantize-then-dequantize. Unlike an
 * ordinary schedule directive, an Approximation deliberately changes the
 * *value* computed, not just how or where it's computed: decode(encode(f))
 * is expected to approximately reproduce f, not exactly reproduce it.
 *
 * encode()/decode() take and return a *vector* of Funcs, not a single Func,
 * even though the common case (a leaf Approximation like a plain quantizer)
 * only ever uses one. This is what makes Compose and Apply below possible:
 * a composed Approximation's inner stage can produce multiple Funcs (e.g. a
 * quantized-values Func plus a separate scale Func), and the next stage
 * needs to be able to consume all of them, or select just one to act on.
 *
 * An Approximation makes no claim about *where* or *when* encode/decode are
 * computed relative to the rest of a pipeline (offline vs fused inline,
 * compute_root vs compute_at) -- that is a scheduling decision, orthogonal
 * to the semantics defined here. Concretely: the same Approximation can be
 * used with encode() computed once, offline, ahead of any other stage (a
 * static weight quantizer) or fused into a producer's inner loop and
 * recomputed on every call (dynamic activation requantization) -- nothing
 * about the interface favors one over the other. See Func::approximate_by()
 * for splicing an Approximation into an existing call graph. */
class Approximation {
public:
    virtual ~Approximation() = default;

    /** Produce the encoded form of `inputs`. EncodeResult::encoded's
     * elements are not required to have the same type, dimensionality, or
     * count as `inputs` -- an Approximation is free to choose a packed
     * representation (a single opaque byte buffer, fields recovered via
     * reinterpret<>() inside decode) or a planar one (multiple typed Funcs,
     * one per field). Either is legitimate; the framework does not
     * decide. */
    virtual EncodeResult encode(std::vector<Func> inputs) = 0;

    /** Reconstruct an approximation of the original Func(s) from their
     * encoded form. See DecodeResult for the constraint on `decoded`'s
     * size, which depends on how this Approximation is used. */
    virtual DecodeResult decode(std::vector<Func> encoded) = 0;
};

namespace Internal {

/** Not for direct use. Type-erases an Approximation-derived value (or an
 * already-type-erased std::unique_ptr<Approximation>, for the rare case
 * where the concrete type is only known at runtime, e.g. chosen by an
 * if/else) into an owned std::unique_ptr<Approximation> -- what lets
 * Compose/Apply's constructors accept a plain mix of concrete Approximation
 * values while still handling runtime-chosen ones, without exposing that
 * distinction as something a caller has to think about. */
// @{
template<typename T>
std::unique_ptr<Approximation> approximation_ptr(T &&value) {
    return std::make_unique<std::decay_t<T>>(std::forward<T>(value));
}
inline std::unique_ptr<Approximation> approximation_ptr(std::unique_ptr<Approximation> value) {
    return value;
}
// @}

}  // namespace Internal

/** The result of Func::approximate_by(): the primary replacement Func
 * (already spliced into every Func in `consumers`), plus every
 * intermediate Func produced by encode()/decode() along the way that needs
 * scheduling (compute_root, compute_at, etc.) -- none of `handles` are
 * part of the Approximation's signature contract, but Halide still
 * requires Funcs with update definitions to be scheduled, and the fusion
 * patterns described on Approximation above (e.g. compute_at-ing the
 * encoded Func into a producer) are only possible if the caller has a
 * handle to schedule. */
struct ApproximationResult {
    Func replacement;
    /** The Func(s) produced by encode() -- the signature-contract boundary
     * between the original values and their approximated form (e.g. a
     * quantizer's packed byte buffer). This is a subset of `handles` (kept
     * there too, so existing code that schedules everything in `handles`
     * doesn't need to change), broken out separately so callers can act on
     * exactly this boundary -- e.g. Pipeline::compute_offline(result.encoded)
     * -- without calling Approximation::encode() themselves. */
    std::vector<Func> encoded;
    std::vector<Func> handles;
};

/** Sequentially composes any number of Approximations into a pipeline:
 * encode() runs `stages` back-to-front (the last stage first, on the
 * original inputs), feeding each stage's encoded output to the one before
 * it; decode() runs the mirror image, front-to-back. So `stages[0]` is the
 * "outermost" stage -- the one whose encode() output is this Compose's own
 * encoded result, and whose decode() input is this Compose's own encoded
 * argument -- and `stages.back()` is "innermost", closest to the original
 * values. This generalizes what used to be a fixed two-stage
 * `Compose(outer, inner)`; that's just the two-element case.
 *
 * Compose owns every stage: each constructor argument is moved into (or, if
 * already a std::unique_ptr<Approximation> -- e.g. because the concrete
 * type was only known at runtime -- taken as) internal storage, so callers
 * don't need to keep named locals alive alongside the Compose itself:
 *
 * \code
 * Compose scheme{
 *     StructPack{...},
 *     Apply{1, 1, 1, Fp16Pack{}},
 *     SymmetricAffineQuantize{block_size, qmax, rounding, anchor},
 * };
 * \endcode
 */
class Compose : public Approximation {
public:
    template<typename... Stages>
    explicit Compose(Stages &&...stages) {
        stages_.reserve(sizeof...(Stages));
        (stages_.push_back(Internal::approximation_ptr(std::forward<Stages>(stages))), ...);
    }

    EncodeResult encode(std::vector<Func> inputs) override;
    DecodeResult decode(std::vector<Func> encoded) override;

private:
    std::vector<std::unique_ptr<Approximation>> stages_;
};

/** Applies `inner` to just the sub-range `[idx, idx + arity)` of a Func
 * vector, passing every other element through unchanged -- e.g. applying a
 * quantizer to just the "shifted" component of an affine (shift + scale)
 * scheme's encoded output while leaving the shift amount itself untouched.
 * `encode_arity`/`decode_arity` (how many Funcs `inner` consumes at that
 * position for each direction) must be given explicitly, since C++ has no
 * way to infer them generically from `inner` itself. Apply owns `inner` --
 * moved in (or taken directly, if already a std::unique_ptr<Approximation>)
 * -- the same way Compose owns its stages. */
class Apply : public Approximation {
public:
    template<typename Inner>
    Apply(int idx, int encode_arity, int decode_arity, Inner &&inner)
        : idx_(idx), encode_arity_(encode_arity), decode_arity_(decode_arity),
          inner_(Internal::approximation_ptr(std::forward<Inner>(inner))) {
    }

    EncodeResult encode(std::vector<Func> inputs) override;
    DecodeResult decode(std::vector<Func> encoded) override;

private:
    int idx_, encode_arity_, decode_arity_;
    std::unique_ptr<Approximation> inner_;
};

}  // namespace Halide

#endif
