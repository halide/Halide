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

class IRMutator;
class IRVisitor;

/** A multi-dimensional ramp. I.e. a ramp of ramps of ramps of ramps...
 *
 * Represents a vector whose lanes are produced by
 *
 *     base + i_0 * strides[0] + i_1 * strides[1] + ...
 *
 * where i_k iterates over [0, lanes[k]) and the innermost dim is dim 0.
 * For example, with base = 0, strides = [1, 100], lanes = [2, 3] the lane
 * sequence is [0, 1, 100, 101, 200, 201].
 *
 * Invariants:
 *   - base is scalar; every entry of strides is scalar and has the same
 *     type as base.
 *   - strides.size() == lanes.size() (this value is dimensions()).
 *   - Each lanes[k] >= 1. An entry of 1 is legal but methods that flatten
 *     (reorder, add, etc.) will remove it.
 *   - dimensions() == 0 represents a scalar (total_lanes() == 1);
 *     to_expr() yields `base` unchanged, and the other methods handle
 *     this case trivially.
 *
 * mul, add, div, mod mutate in place. mul always succeeds; add/div/mod
 * return false when the result isn't expressible as a multiramp (leaving
 * *this undefined). */
struct MultiRamp {
    Expr base;
    std::vector<Expr> strides;
    std::vector<int> lanes;

    /** Multiply by a scalar. Always a multiramp. */
    void mul(const Expr &e);

    /** Add another MultiRamp elementwise. Returns false if the result isn't
     * a multiramp (which happens when the two input shapes have no common
     * refinement). */
    bool add(const MultiRamp &other);

    /** Floor-divide by a scalar. The main use case is recognizing
     * downsampling reductions like `f(r/4) += g(r)` as multiramps, so that
     * the vectorize pass can handle them as within-vector reductions.
     *
     * Returns false if the denominator isn't a positive integer constant,
     * or if the quotient isn't a multiramp. The result may have one more
     * dim than the input (a single split may be introduced per input dim,
     * e.g. ramp(0,2,6)/4 requires splitting a dim of extent 6 into 2x3
     * because the quotient changes mid-dim). See div_or_mod_impl in
     * MultiRamp.cpp for the derivation. O(d). */
    bool div(const Expr &k);

    /** Euclidean mod by a scalar. Returns false if the denominator isn't a
     * positive integer constant, or if the remainder isn't a multiramp.
     * Same shape transformations as div. Rare cases where the remainder is
     * a multiramp but the quotient isn't are not recognized here. O(d). */
    bool mod(const Expr &k);

    /** Construct an Expr which gives whether one multiramp is equal to
     * another in every lane. Assumes the total lane count matches. Returns
     * a symbolic Expr (not a bool) matching operator== semantics on
     * Exprs. */
    Expr operator==(const MultiRamp &other) const;

    /** Remove dim `d`, adding `v * strides[d]` to base. Pass v = 0 for the
     * first slice along that dim, or a Variable to get a parameterized
     * slice. */
    void slice(int d, Expr v);

    /** Construct an Expr telling us whether the lanes are all unique. A
     * sufficient condition is that there is an ordering of the dims along
     * which each stride is greater than the sum of the spans of earlier
     * dims (span of dim k = |strides[k]| * (lanes[k] - 1)). We check all
     * dim orderings and OR the resulting conditions together (ignoring
     * base, since base is a uniform offset and doesn't affect uniqueness
     * within the ramp). If this simplifies to const_false the lanes may
     * or may not actually alias. */
    Expr alias_free() const;

    /** Information about one peeled dim, produced by alias_free_slice.
     * `dim` is the dim's position in the *pre-call* MultiRamp. */
    struct PeeledDim {
        Expr stride;
        int lanes;
        int dim;
    };

    /** Build an alias-free slice of *this by greedily adding dims innermost
     * to outermost, keeping a dim only if the slice remains alias-free
     * after it's added. Replace *this with the resulting slice, and return
     * a description of the dims that weren't kept (innermost first).
     * Always succeeds; *this may be reduced to a 0-dim scalar if no prefix
     * of dims is alias-free. The omitted dims' contributions are NOT
     * folded into base — callers usually want to add back
     * `var * omitted.stride` per omitted dim before using *this. */
    std::vector<PeeledDim> alias_free_slice();

    /** No-op returning 0 if the stride-1 dim is already innermost (or
     * there isn't one). Otherwise rotate the dims so the stride-1 dim
     * moves to position 0, with the previously-inner dims moved to the
     * outermost end, and return A = the product of those previously-inner
     * dims' lane counts. After this call,
     * Shuffle::make_transpose(new_to_expr(), total_lanes / A) reconstructs
     * a vector in the old lane order from one in the new order. */
    int rotate_stride_one_innermost();

    /** The dimensionality. May be lower than you expected, because this
     * gets flattened when possible by the operations above. */
    int dimensions() const;

    /** The product of all the lane counts. */
    int total_lanes() const;

    /** The multiramp as a nested series of ramps. */
    Expr to_expr() const;

    /** Flatten the multiramp into a vector of 1D Ramps — one per outer
     * multi-index, each with inner_lanes = lanes[0] and stride =
     * strides[0]. Ramps are returned in this MultiRamp's lane order:
     * concat'ing the returned Ramps reproduces the full lane sequence. The
     * caller is responsible for any prior mutation/simplification of `base`
     * and `strides` (the Ramps reference them directly). */
    std::vector<Expr> flatten() const;

    /** Reorder this MultiRamp's dimensions in place. perm[k] is the index
     * into this's current dims that becomes the k-th dim after reordering
     * (innermost first, as always). perm must be a permutation of
     * {0, ..., dimensions()-1}. E.g. with dims [s0, s1, s2] and
     * perm = [2, 0, 1], after reorder the new dims are [s2, s0, s1]. */
    void reorder(const std::vector<int> &perm);

    /** Pass an IRVisitor through all Exprs referenced (base and each
     * stride). Note that base and strides are scalar but may nonetheless
     * contain nested vector reductions. */
    void accept(IRVisitor *visitor) const;

    /** Pass an IRMutator through all Exprs referenced, replacing base and
     * strides with the mutated results. Note that base and strides are
     * scalar but may nonetheless contain nested vector reductions. */
    void mutate(IRMutator *mutator);

    /** Given a permutation `perm`, return shuffle indices `idx` such that
     * if `p` is a copy of `*this` with `reorder(perm)` applied, then
     *
     *     Shuffle::make({p.to_expr()}, idx)
     *
     * produces the same vector of lane values as `this->to_expr()`. I.e.
     * given a vector in the permuted lane order, the returned indices put
     * it back into this MultiRamp's original lane order. */
    std::vector<int> shuffle_from_permuted(const std::vector<int> &perm) const;

    /** Return shuffle indices `idx` such that
     *
     *     Shuffle::make({this->to_expr()}, idx)
     *
     * produces the same vector of lane values as a copy of *this with
     * slice(dims[j], pos[j]) applied for each j. Since slicing reduces
     * the lane count, the shuffle selects the subset of *this's lanes
     * whose coordinate along dim `dims[j]` equals `pos[j]` for all j.
     * `dims` and `pos` must have the same length and `dims` must list
     * distinct dim indices. */
    std::vector<int> shuffle_from_slice(const std::vector<int> &dims,
                                        const std::vector<int> &pos) const;
};

/** Check if a vector Expr is a multiramp, and assign to result if so.
 * Contract: on failure, *result is left in an unspecified state; callers
 * must not read *result unless is_multiramp returned true. */
bool is_multiramp(const Expr &e, const Scope<Expr> &scope, MultiRamp *result);

}  // namespace Internal
}  // namespace Halide

#endif
