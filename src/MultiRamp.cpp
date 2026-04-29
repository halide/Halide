#include "MultiRamp.h"

#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "ModulusRemainder.h"
#include "Simplify.h"
#include "Util.h"

#include <numeric>
#include <optional>

namespace Halide {
namespace Internal {

namespace {

// Collapse adjacent dims whose strides align: if the outer stride equals
// inner_stride · inner_lanes, the two dims describe a single flat dim and
// can be merged. Keeps the output tidy; doesn't affect what values the
// MultiRamp represents.
void collapse_adjacent_dims(MultiRamp *m) {
    for (size_t i = 1; i < m->lanes.size();) {
        Expr want_outer = simplify(m->strides[i - 1] * m->lanes[i - 1]);
        if (equal(m->strides[i], want_outer)) {
            m->lanes[i - 1] *= m->lanes[i];
            m->strides.erase(m->strides.begin() + i);
            m->lanes.erase(m->lanes.begin() + i);
        } else {
            i++;
        }
    }
}

}  // namespace

// Multiramps with compatible lanes form a vector space. Here is scalar multiplication.
void MultiRamp::mul(const Expr &e) {
    internal_assert(e.type().is_scalar());
    base *= e;
    for (Expr &s : strides) {
        s *= e;
    }
}

// And here is vector addition. Returns false when the two shapes have no
// common refinement (the sum is not a multiramp). Adding multiramps with
// different total lane counts is a caller error and triggers an assertion.
bool MultiRamp::add(const MultiRamp &other) {
    // We walk through both ramps' dimensions innermost-to-outermost, consuming
    // gcd(a_lanes, b_lanes) of lanes at a time. When a dimension is only
    // partially consumed, the remaining part of that dimension corresponds to
    // an "outer" sub-dim in the refined shape and its stride must be scaled
    // by the factor just consumed.
    internal_assert(total_lanes() == other.total_lanes())
        << "MultiRamp::add: total lane counts must match (" << total_lanes()
        << " vs " << other.total_lanes() << ")";
    if (lanes.empty()) {
        // Both are 0-dim scalars.
        base = simplify(base + other.base);
        return true;
    }
    MultiRamp result;
    result.base = simplify(base + other.base);
    size_t ai = 0, bi = 0;
    int a_lanes = lanes[0], b_lanes = other.lanes[0];
    Expr a_stride = strides[0], b_stride = other.strides[0];
    while (true) {
        int next_lanes = gcd(a_lanes, b_lanes);
        if (next_lanes == 1) {
            // The two next lanes are coprime, e.g:
            //   [0, 1, 2, 100, 101, 102] + [0, 1, 100, 101, 200, 201]
            // which has no common refinement.
            return false;
        }
        result.strides.emplace_back(simplify(a_stride + b_stride));
        result.lanes.push_back(next_lanes);
        a_lanes /= next_lanes;
        b_lanes /= next_lanes;
        bool a_done = false, b_done = false;
        if (a_lanes == 1) {
            ai++;
            if (ai >= lanes.size()) {
                a_done = true;
            } else {
                a_lanes = lanes[ai];
                a_stride = strides[ai];
            }
        } else {
            // Remaining portion of current A-dim has a scaled stride.
            a_stride = simplify(a_stride * next_lanes);
        }
        if (b_lanes == 1) {
            bi++;
            if (bi >= other.lanes.size()) {
                b_done = true;
            } else {
                b_lanes = other.lanes[bi];
                b_stride = other.strides[bi];
            }
        } else {
            b_stride = simplify(b_stride * next_lanes);
        }
        if (a_done && b_done) {
            collapse_adjacent_dims(&result);
            *this = std::move(result);
            return true;
        }
        // The up-front lane-count check ensures both sides always exhaust
        // together, so neither side should be done here.
    }
}

namespace {

// Divide (or mod) a MultiRamp by a positive integer k. Returns a new
// MultiRamp, or false if the quotient/remainder isn't itself a multiramp.
// Shared core of div_by and mod_by.
//
// Precondition: the base is a known multiple of k. Otherwise we return false.
//
// Mental model
// ------------
// Picture the integers laid out in buckets of size k: [0, k), [k, 2k), ....
// Dividing by k asks "which bucket?", modding by k asks "where inside the
// bucket?". The base sits at the left edge of some bucket. We want every
// lane of the result to remain an affine function of the multi-index — i.e.
// a multiramp. Whether that's possible depends on how the input dims move
// the lanes around relative to those buckets.
//
// Two kinds of input dim
// ----------------------
// "Pure-carry" dim: stride s is itself a multiple of k. Every step crosses
// a whole number of buckets, so the output stride for this dim is just s/k.
// These are boring in a good way.
//
// "Flex" dim: stride s isn't a multiple of k. Write s = k·q + r with
// r = s mod k in [0, k). A step advances the bucket index by q and shifts
// the position-inside-the-bucket by r. If every lane along this dim still
// lives in the same bucket, the output stride is q and the intra-bucket
// wiggle washes out under /k. The danger is that the position eventually
// exceeds k-1 — at which point the floor jumps and the result isn't a
// multiramp.
//
// Worked example
// --------------
// base 0, stride 2, lanes 6, k = 4. Values 0, 2, 4, 6, 8, 10.
//
// Treat it as one flat 6-lane dim and it's doomed: the positions inside
// the bucket would be 0, 2, 4, 6, ... — already past k-1 = 3 at lane 2.
//
// But we can reshape the 6 lanes as (inner 2 × outer 3). The inner stride
// stays 2, and the outer stride becomes 2·2 = 4 — a whole bucket. Now the
// outer dim is pure-carry, and the inner dim only shows positions 0 and 2,
// safely inside [0, 4). The result is base 0, strides [0, 1], lanes [2, 3],
// which expands to 0, 0, 1, 1, 2, 2. That matches the per-lane division.
//
// The budget
// ----------
// Because the base is a bucket boundary, every lane starts at position 0.
// At the far corner of the iteration box each flex dim contributes r·(n-1)
// to the position, and the positions have to stay ≤ k-1 everywhere. So the
// flex dims share a single budget of k-1; each one spends r·(n-1) of it.
// If they all fit, we're done.
//
// Joint fit: base 0, strides [2, 3], lanes [2, 2], k = 6.
//   Input values:  0, 2, 3, 5       (all in bucket [0, 6))
//   / 6:           0, 0, 0, 0       (a multiramp with strides [0, 0])
//   Inner spends 2·(2-1) = 2 of the budget, outer spends 3·(2-1) = 3. Total
//   5 = k-1, just fitting.
//
// Joint failure: base 0, strides [2, 5], lanes [2, 2], k = 6.
//   Input values:  0, 2, 5, 7       (7 is in the next bucket)
//   / 6:           0, 0, 0, 1       (not a multiramp of any shape)
//   Inner spends 2, outer spends 5. Each alone would fit (≤ 5), but
//   together they want 7 > 5. Return false.
//
// The split trick
// ---------------
// When a single dim's r·(n-1) blows the budget by itself, here's the
// escape. Let p = k / gcd(k, r) — the smallest number of stride-s steps
// that reach a bucket boundary (since p·s ≡ p·r (mod k), and we want that
// to be 0). We re-view the dim of lanes n as (inner p × outer n/p) with
// strides (s, s·p). The outer stride s·p is a whole number of buckets, so
// the outer dim is pure-carry. Only the inner still spends budget, and
// only r·(p-1) of it. If even that doesn't fit, we give up.
//
// Algorithm
// ---------
// Walk input dims innermost-first, with budget = k-1. For each dim we only
// need to know r = s mod k (not s itself) — so a symbolic stride is fine as
// long as we can pin down its residue modulo k. If we can't, fail. For the
// first case that applies, emit its output; if none, fail.
//
//   (a) r = 0 (pure carry)             → emit (s/k, n).
//   (b) r·(n-1) ≤ budget               → emit (s/k, n);
//                                          budget -= r·(n-1).
//   (c) p = k/gcd(k,r); 1 < p < n,
//       p divides n, r·(p-1) ≤ budget  → emit inner (s/k, p) and
//                                          outer (s·p/k, n/p);
//                                          budget -= r·(p-1).
//   (d) otherwise                      → return false.
//
// Output base is base/k for div, 0 for mod. For mod, emit r in place of
// s/k and 0 in place of s·p/k; the shape is the same.
//
// Finally, collapse any adjacent output dims where the outer stride is
// inner_stride · inner_lanes — e.g. pure-carry dims with matching strides
// from two consecutive inputs, or a split's outer half lining up with the
// next input's contribution. This just keeps the output tidy; it doesn't
// affect which inputs we accept.
//
// Rejection examples
// ------------------
// base 0, stride 1, lanes 5, / 2:
//   Input values:  0, 1, 2, 3, 4
//   / 2:           0, 0, 1, 1, 2     (not a multiramp of any shape)
//   p = 2 doesn't divide 5, and the flat dim would spill immediately
//   (r·(n-1) = 4 > 1 = budget). Return false.
//
// base 3, stride 2, lanes 2, / 4:
//   Input values:  3, 5
//   / 4:           0, 1              (does happen to be a multiramp, but
//                                     our algorithm requires an aligned
//                                     base and skips this case)
//   Return false before even looking at the dims.
bool div_or_mod_impl(MultiRamp *self, const Expr &k_expr, bool is_div) {
    auto ck = as_const_int(k_expr);
    if (!ck || *ck <= 0) {
        return false;
    }
    int64_t k = *ck;
    Type t = self->base.type();

    // Aligned-base assumption: require base to be a known multiple of k.
    int64_t b_mod = 0;
    if (!reduce_expr_modulo(self->base, k, &b_mod) || b_mod != 0) {
        return false;
    }

    MultiRamp result;
    result.base = is_div ? simplify(self->base / (int)k) : make_zero(t);

    // Residual budget: how much room is left inside the single k-bucket
    // starting at the base. Starts at k-1 and shrinks as each non-pure-carry
    // dim spends r·(lanes-1) of it.
    int64_t budget = k - 1;

    for (size_t j = 0; j < self->strides.size(); j++) {
        const Expr &s = self->strides[j];
        int n = self->lanes[j];

        // Everything below only needs s mod k, never s itself. So it's fine
        // for s to be symbolic, as long as we can pin down its residue.
        int64_t r = 0;
        if (!reduce_expr_modulo(s, k, &r)) {
            return false;
        }

        // Case (a): pure carry.
        if (r == 0) {
            result.strides.push_back(is_div ? simplify(s / (int)k) : make_zero(t));
            result.lanes.push_back(n);
            continue;
        }

        // Case (b): whole dim fits in the remaining budget. Note that (b)
        // and (c) below are mutually exclusive — if the whole dim fits, n
        // is necessarily ≤ p, which means case (c) couldn't apply anyway.
        // So their order here doesn't matter for which inputs we accept.
        if (r * (n - 1) <= budget) {
            result.strides.push_back(is_div ? simplify(s / (int)k) : make_const(t, r));
            result.lanes.push_back(n);
            budget -= r * (n - 1);
            continue;
        }

        // Case (c): split into (inner = p, outer = n/p). The smallest p with
        // p·s ≡ 0 (mod k) only depends on r, since p·s ≡ p·r (mod k). So
        // p = k / gcd(k, r).
        int64_t p = k / gcd(k, r);

        // For r ∈ (0, k), gcd(k, r) ≤ r < k, so p ≥ 2.
        internal_assert(p > 1);

        if (p >= (int64_t)n) {
            // The smallest split that would work is >= than the number of lanes
            // we have in this dimension.
            return false;
        }

        if (n % p) {
            // p must divide n to split n by p. Any larger
            // split size would also be a multiple of p, so
            // if p does not divide n, no valid split size
            // divides n.
            return false;
        }

        if (r * (p - 1) > budget) {
            // We ran out of budget.
            return false;
        }

        // Inner half: residual fits after shrinking to size p.
        result.strides.push_back(is_div ? simplify(s / (int)k) : make_const(t, r));
        result.lanes.push_back((int)p);
        budget -= r * (p - 1);

        // Outer half: s·p is a multiple of k by construction, so this divides
        // exactly (though Halide's simplifier may or may not fold it).
        result.strides.push_back(is_div ? simplify((s * (int)p) / (int)k) : make_zero(t));
        result.lanes.push_back((int)(n / p));
    }

    collapse_adjacent_dims(&result);
    *self = std::move(result);
    return true;
}

}  // namespace

bool MultiRamp::div(const Expr &k) {
    return div_or_mod_impl(this, k, /*is_div=*/true);
}

bool MultiRamp::mod(const Expr &k) {
    return div_or_mod_impl(this, k, /*is_div=*/false);
}

namespace {
std::optional<Expr> unbroadcast(const Expr &e) {
    if (e.type().is_scalar()) {
        return e;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return unbroadcast(b->value);
    } else {
        return std::nullopt;
    }
}

// Internal is_multiramp. May leave *result in a partial state on failure;
// the public is_multiramp below protects callers by only committing on
// success. Recursive calls go through the public wrapper, so each branch
// here can assume *result is either freshly initialized (on entry) or
// freshly filled by a successful recursion.
bool is_multiramp_impl(const Expr &e, const Scope<Expr> &scope, MultiRamp *result) {
    Type elem_t = e.type().element_of();
    if (e.type().is_scalar()) {
        result->base = e;
        return true;
    } else if (const Variable *v = e.as<Variable>()) {
        if (const Expr *e = scope.find(v->name)) {
            return is_multiramp(*e, scope, result);
        }
    } else if (const Broadcast *b = e.as<Broadcast>();
               b && is_multiramp(b->value, scope, result)) {
        result->strides.push_back(make_zero(elem_t));
        result->lanes.push_back(b->lanes);
        return true;
    } else if (const Ramp *r = e.as<Ramp>()) {
        if (auto stride = unbroadcast(r->stride)) {
            if (is_multiramp(r->base, scope, result)) {
                result->strides.push_back(*stride);
                result->lanes.push_back(r->lanes);
                return true;
            }
        }
    } else if (const Add *a = e.as<Add>()) {
        MultiRamp rb;
        if (is_multiramp(a->a, scope, result) &&
            is_multiramp(a->b, scope, &rb)) {
            return result->add(rb);
        }
    } else if (const Sub *s = e.as<Sub>()) {
        // Convert to Add to reuse logic above.
        MultiRamp rb;
        if (is_multiramp(s->a, scope, result) &&
            is_multiramp(s->b, scope, &rb)) {
            rb.mul(make_const(elem_t, -1));
            return result->add(rb);
        }
    } else if (const Mul *m = e.as<Mul>()) {
        // Try each side as the scalar factor. The public wrapper's
        // untouched-on-failure guarantee means a failed first attempt
        // leaves *result clean for the second.
        if (auto b = unbroadcast(m->b);
            b && is_multiramp(m->a, scope, result)) {
            result->mul(*b);
            return true;
        }
        if (auto a = unbroadcast(m->a);
            a && is_multiramp(m->b, scope, result)) {
            result->mul(*a);
            return true;
        }
    } else if (const Div *d = e.as<Div>()) {
        if (auto denom = unbroadcast(d->b)) {
            if (is_multiramp(d->a, scope, result)) {
                return result->div(*denom);
            }
        }
    } else if (const Mod *m = e.as<Mod>()) {
        if (auto denom = unbroadcast(m->b)) {
            if (is_multiramp(m->a, scope, result)) {
                return result->mod(*denom);
            }
        }
    }
    return false;
}
}  // namespace

bool is_multiramp(const Expr &e, const Scope<Expr> &scope, MultiRamp *result) {
    // Wrap the impl so that callers get a clean "untouched on failure"
    // contract regardless of how the impl leaves its scratch space.
    MultiRamp tmp;
    if (is_multiramp_impl(e, scope, &tmp)) {
        *result = std::move(tmp);
        return true;
    }
    return false;
}

Expr MultiRamp::operator==(const MultiRamp &other) const {
    // Construct the difference, and check if all strides are zero.
    MultiRamp diff = other;
    diff.mul(-1);
    if (!diff.add(*this)) {
        return const_false();
    }
    Expr c = diff.base == 0;
    for (const Expr &s : diff.strides) {
        c = c && s == 0;
    }
    return simplify(c);
}

void MultiRamp::slice(int d, const Expr &v) {
    internal_assert(d >= 0 && d < (int)strides.size());
    internal_assert(v.type() == base.type());
    base += v * strides[d];
    strides.erase(strides.begin() + d);
    lanes.erase(lanes.begin() + d);
    collapse_adjacent_dims(this);
}

Expr MultiRamp::alias_free() const {
    // A sufficient condition: there exists an ordering of dims such that
    // each stride's absolute value is strictly greater than the sum of the
    // spans of all earlier dims, where span(k) = |strides[k]| * (lanes[k] −
    // 1). Under such an ordering the lanes enumerate distinct offsets in an
    // interval-tree fashion. In principle we'd only need to test the
    // ordering with increasing |strides|, but symbolic strides leave the
    // ordering unknown, so we try all permutations and OR the conditions.
    // (The permutation count is small in practice — one dim per nested
    // loop.) This ignores base, which is fine for uniqueness within the
    // ramp (base is a uniform offset).

    if (lanes.empty()) {
        return const_true();
    }
    int d = (int)lanes.size();
    std::vector<int> perm(d);
    std::iota(perm.begin(), perm.end(), 0);
    Expr result = const_false();
    do {
        Expr cond = (strides[perm[0]] != 0);
        Expr accum = make_zero(base.type());  // running sum of |s_k|*(n_k − 1)
        for (int j = 0; j < d; j++) {
            Expr s = strides[perm[j]];
            Expr abs_s = abs(s);
            if (j > 0) {
                cond = cond && (abs_s > accum);
            }
            accum = accum + abs_s * (lanes[perm[j]] - 1);
        }
        result = result || cond;
    } while (std::next_permutation(perm.begin(), perm.end()));
    return simplify(result);
}

std::vector<MultiRamp::PeeledDim> MultiRamp::alias_free_slice() {
    // Greedy: starting from an empty MultiRamp (same base), try adding dims
    // one by one from innermost to outermost. Any dim that would break the
    // alias-free condition is peeled off instead. Stride-zero dims always
    // break alias-freedom (except as the single dim of a 1-dim ramp, which
    // is a scalar), so we fast-path them to skip the can_prove call.
    std::vector<PeeledDim> peeled;
    MultiRamp remaining;
    remaining.base = base;
    for (int i = 0; i < dimensions(); i++) {
        bool must_peel = is_const_zero(strides[i]) && !remaining.lanes.empty();
        if (!must_peel) {
            remaining.strides.push_back(strides[i]);
            remaining.lanes.push_back(lanes[i]);
            if (can_prove(remaining.alias_free())) {
                continue;
            }
            remaining.strides.pop_back();
            remaining.lanes.pop_back();
        }
        peeled.push_back(PeeledDim{strides[i], lanes[i], i});
    }
    *this = std::move(remaining);
    return peeled;
}

int MultiRamp::rotate_stride_one_innermost() {
    int k = -1;
    for (int i = 0; i < dimensions(); i++) {
        if (is_const_one(strides[i])) {
            k = i;
            break;
        }
    }
    if (k <= 0) {
        return 0;
    }
    int A = 1;
    for (int i = 0; i < k; i++) {
        A *= lanes[i];
    }
    int d = dimensions();
    std::vector<int> perm(d);
    std::iota(perm.begin(), perm.end(), 0);
    std::rotate(perm.begin(), perm.begin() + k, perm.end());
    reorder(perm);
    return A;
}

int MultiRamp::dimensions() const {
    return (int)strides.size();
}

int MultiRamp::total_lanes() const {
    int prod = 1;
    for (int l : lanes) {
        prod *= l;
    }
    return prod;
}

Expr MultiRamp::to_expr() const {
    Expr e = base;
    for (int i = 0; i < dimensions(); i++) {
        if (is_const_zero(strides[i])) {
            e = Broadcast::make(e, lanes[i]);
        } else if (e.type().is_scalar()) {
            e = Ramp::make(e, strides[i], lanes[i]);
        } else {
            e = Ramp::make(e, Broadcast::make(strides[i], e.type().lanes()), lanes[i]);
        }
    }
    return e;
}

void MultiRamp::reorder(const std::vector<int> &perm) {
    int d = dimensions();
    internal_assert((int)perm.size() == d) << "perm size mismatch\n";
    std::vector<Expr> new_strides;
    std::vector<int> new_lanes;
    new_strides.reserve(d);
    new_lanes.reserve(d);
    for (int k = 0; k < d; k++) {
        internal_assert(perm[k] >= 0 && perm[k] < d) << "perm out of range\n";
        new_strides.push_back(std::move(strides[perm[k]]));
        new_lanes.push_back(lanes[perm[k]]);
    }
    strides = std::move(new_strides);
    lanes = std::move(new_lanes);
}

void MultiRamp::accept(IRVisitor *visitor) const {
    base.accept(visitor);
    for (const Expr &s : strides) {
        s.accept(visitor);
    }
}

void MultiRamp::mutate(IRMutator *mutator) {
    base = (*mutator)(base);
    for (Expr &s : strides) {
        s = (*mutator)(s);
    }
}

std::vector<int> MultiRamp::shuffle_from_permuted(const std::vector<int> &perm) const {
    // For each output lane n (in *this's lane order), we want the shuffle to
    // pull from the input (permuted) vector's lane that represents the same
    // multi-index. Decompose n into multi-index (i_0, ..., i_{d-1}) using
    // this->lanes (innermost first); the matching multi-index in the permuted
    // MultiRamp is (j_k) with j_k = i_{perm[k]}, flattened with
    // this->lanes[perm[k]] as its innermost lane counts.
    int d = dimensions();
    internal_assert((int)perm.size() == d);
    std::vector<int> indices;
    indices.reserve(total_lanes());
    for_each_coordinate(lanes, [&](const std::vector<int> &coord) {
        int permuted_flat = 0, M = 1;
        for (int k = 0; k < d; k++) {
            permuted_flat += coord[perm[k]] * M;
            M *= lanes[perm[k]];
        }
        indices.push_back(permuted_flat);
    });
    return indices;
}

std::vector<Expr> MultiRamp::flatten() const {
    int d = dimensions();
    if (d == 0) {
        return {base};
    }
    int inner_lanes = lanes[0];
    std::vector<int> outer_sizes(lanes.begin() + 1, lanes.end());
    std::vector<Expr> result;
    result.reserve(total_lanes() / inner_lanes);
    for_each_coordinate(outer_sizes, [&](const std::vector<int> &coord) {
        Expr offset_base = base;
        for (size_t k = 0; k < coord.size(); k++) {
            offset_base = offset_base + coord[k] * strides[k + 1];
        }
        result.push_back(Ramp::make(offset_base, strides[0], inner_lanes));
    });
    return result;
}

std::vector<int> MultiRamp::shuffle_from_slice(const std::vector<int> &dims,
                                               const std::vector<int> &pos) const {
    // For each output lane n (in the sliced MultiRamp's lane order), we want
    // the shuffle to pull from the lane of *this whose multi-index matches
    // n in the free (non-sliced) dims, and has the specified values in the
    // sliced dims.
    internal_assert(dims.size() == pos.size());
    int d = dimensions();
    std::vector<int> fixed(d, -1);
    for (size_t j = 0; j < dims.size(); j++) {
        int dd = dims[j];
        internal_assert(dd >= 0 && dd < d);
        internal_assert(pos[j] >= 0 && pos[j] < lanes[dd]);
        internal_assert(fixed[dd] == -1) << "duplicate dim in shuffle_from_slice\n";
        fixed[dd] = pos[j];
    }
    // Sizes of the free (non-fixed) dims, in the same order as they
    // appear in the full dim list.
    std::vector<int> free_sizes;
    for (int k = 0; k < d; k++) {
        if (fixed[k] == -1) {
            free_sizes.push_back(lanes[k]);
        }
    }
    std::vector<int> indices;
    for_each_coordinate(free_sizes, [&](const std::vector<int> &free_coord) {
        int flat = 0, M = 1;
        size_t fj = 0;
        for (int k = 0; k < d; k++) {
            int ik = (fixed[k] != -1) ? fixed[k] : free_coord[fj++];
            flat += ik * M;
            M *= lanes[k];
        }
        indices.push_back(flat);
    });
    return indices;
}

}  // namespace Internal
}  // namespace Halide
