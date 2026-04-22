#ifndef HALIDE_MULTI_RAMP_H
#define HALIDE_MULTI_RAMP_H

/** \file
 * Defines the MultiRamp IR helper — a multi-dimensional ramp recognised and
 * manipulated by the vectorization pass and its callers.
 */

#include "Expr.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** A multi-dimensional ramp. I.e. a ramp of ramps of ramps of ramps...
 *
 * The scalar-producing operations (mul, add, div, mod) all mutate the
 * MultiRamp in place. mul always succeeds; add/div/mod return false when
 * the result isn't expressible as a multiramp (leaving *this undefined). */
struct MultiRamp {
    Expr base;
    // The first stride is the innermost one. So for example, if the base is
    // zero, strides are [1, 100] and the extents are [2, 3], the IR node is a
    // vector with lanes: [0, 1, 100, 101, 200, 201]
    std::vector<Expr> strides;
    std::vector<int> lanes;

    // Multiply by a scalar. Always a multiramp.
    void mul(const Expr &e);

    // Add another MultiRamp elementwise. Returns false if the result isn't a
    // multiramp (which happens when the two input shapes have no common
    // refinement).
    bool add(const MultiRamp &other);

    // Floor-divide by a scalar. Returns false if the denominator isn't a
    // positive integer constant, or if the quotient isn't a multiramp. The
    // result may have one more dim than the input (a single split may be
    // introduced per input dim). O(d).
    bool div(const Expr &k);

    // Euclidean mod by a scalar. Returns false if the denominator isn't a
    // positive integer constant, or if the remainder isn't a multiramp.
    // Same shape as div. Rare cases where the remainder is a multiramp but
    // the quotient isn't are not recognized here. O(d).
    bool mod(const Expr &k);

    // Construct an Expr which gives whether one multiramp is equal to another
    // in every lane. Assumes the total lane count matches.
    Expr operator==(const MultiRamp &other) const;

    // Remove a dimension, replacing it with the given scalar expression
    // (e.g. pass v = 0 to get the first slice along that dimension, pass v =
    // some var to get a parameterized slice along that dimension).
    void slice(int d, Expr v);

    // Construct an Expr telling us whether the lanes are all unique. This
    // expression being false is conservative: it doesn't imply aliasing, only
    // that we couldn't construct the tightest condition for it in closed form.
    Expr alias_free() const;

    // The dimensionality. May be lower than you expected, because this gets
    // flattened when possible by the operations above.
    int dimensions() const;

    // The product of all the lane counts
    int total_lanes() const;

    // The multiramp as a nested series of ramps
    Expr to_expr() const;

    // Flatten the multiramp into a vector of 1D Ramps — one per outer
    // multi-index, each with inner_lanes = lanes[0] and stride = strides[0].
    // Ramps are returned in this MultiRamp's lane order: concat'ing the
    // returned Ramps reproduces the full lane sequence. The caller is
    // responsible for any prior mutation/simplification of `base` and
    // `strides` (the Ramps reference them directly).
    std::vector<Expr> flatten() const;

    // Reorder this MultiRamp's dimensions in place. perm[k] is the index
    // into this's current dims that becomes the k-th dim after reordering
    // (innermost first, as always). perm must be a permutation of
    // {0, ..., dimensions()-1}.
    void reorder(const std::vector<int> &perm);

    // Given a permutation `perm`, return shuffle indices `idx` such that if
    // `p` is a copy of `*this` with `reorder(perm)` applied, then
    //     Shuffle::make({p.to_expr()}, idx)
    // produces the same vector of lane values as `this->to_expr()`. In other
    // words: given a vector in the permuted lane order, the returned indices
    // put it back into this MultiRamp's original lane order.
    std::vector<int> shuffle_from_permuted(const std::vector<int> &perm) const;

    // Given a dimension `d` and a position `pos` within it, return shuffle
    // indices `idx` such that
    //     Shuffle::make({this->to_expr()}, idx)
    // produces the same vector of lane values as a copy of *this with
    // slice(d, pos) applied. Since slicing reduces the lane count, the
    // shuffle selects the subset of *this's lanes whose d-th coordinate
    // equals `pos`.
    std::vector<int> shuffle_from_slice(int d, int pos) const;

    // Variant that slices multiple dims simultaneously. Returns shuffle
    // indices selecting the lanes of *this where dim `dims[j]` equals
    // `pos[j]` for all j. `dims` and `pos` must have the same length and
    // `dims` must list distinct dim indices.
    std::vector<int> shuffle_from_slice(const std::vector<int> &dims,
                                        const std::vector<int> &pos) const;
};

/** Check if a vector Expr is a multiramp, and assign to result if so. */
bool is_multiramp(const Expr &e, const Scope<Expr> &scope, MultiRamp *result);

}  // namespace Internal
}  // namespace Halide

#endif
