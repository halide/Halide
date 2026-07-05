#ifndef HALIDE_APPROXIMATION_H
#define HALIDE_APPROXIMATION_H

/** \file
 * Defines Approximation, a core interface for lossy, quantified
 * Func-to-Func transformations (e.g. a quantize/dequantize round trip).
 * See Func::approximate_by(), which splices such a round trip into an
 * existing call graph, and doc/ApproximationDesign.md for the design
 * rationale.
 */

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

/** The result of Approximation::decode(): decoded[0] is the round-trip
 * replacement for whatever Func was originally encoded, plus any
 * additional scheduling-only handles. */
struct DecodeResult {
    std::vector<Func> decoded;
    std::vector<Func> handles;
};

/** Approximation is the base class for a lossy, quantified transformation
 * of a Func's values -- e.g. quantize-then-dequantize. Unlike an ordinary
 * schedule directive, an Approximation deliberately changes the *value*
 * computed, not just how or where it's computed: decode(encode(f)) is
 * expected to approximately reproduce f, not exactly reproduce it.
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

    /** Produce the encoded form of `f`. EncodeResult::encoded's first
     * element is not required to have the same type or dimensionality as
     * `f` -- an Approximation is free to choose a packed representation
     * (a single opaque byte buffer, fields recovered via reinterpret<>()
     * inside decode) or a planar one (multiple typed Funcs, one per
     * field). Either is legitimate; the framework does not decide. */
    virtual EncodeResult encode(Func f) = 0;

    /** Reconstruct an approximation of the original Func from its encoded
     * form. DecodeResult::decoded must contain exactly one Func, and it
     * must match the original Func's argument list and value type(s)
     * exactly, so it can be used in its place. */
    virtual DecodeResult decode(std::vector<Func> encoded) = 0;
};

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
    std::vector<Func> handles;
};

}  // namespace Halide

#endif
