#ifndef HALIDE_INTERVAL_H
#define HALIDE_INTERVAL_H

/** \file
 * Defines the Interval class
 */

#include "Expr.h"
#include "Util.h"

namespace Halide {
namespace Internal {

/** A class to represent ranges of Exprs. Can be unbounded above or below. */
struct Interval {

    /** Exprs to represent positive and negative infinity */
#ifdef COMPILING_HALIDE
    static HALIDE_ALWAYS_INLINE Expr pos_inf() {
        return pos_inf_expr;
    }
    static HALIDE_ALWAYS_INLINE Expr neg_inf() {
        return neg_inf_expr;
    }
#else
    static Expr pos_inf() {
        return pos_inf_noinline();
    }
    static Expr neg_inf() {
        return neg_inf_noinline();
    }
#endif

    /** The lower and upper bound of the interval. They are included
     * in the interval. */
    Expr min, max;

    /** A default-constructed Interval is everything */
    Interval()
        : min(neg_inf()), max(pos_inf()) {
    }

    /** Construct an interval from a lower and upper bound. */
    Interval(Expr min, Expr max)
        : min(min), max(max) {
        internal_assert(min.defined() && max.defined());
    }

    /** The interval representing everything. */
    static Interval everything() {
        return Interval(neg_inf(), pos_inf());
    }

    /** The interval representing nothing. */
    static Interval nothing() {
        return Interval(pos_inf(), neg_inf());
    }

    /** Construct an interval representing a single point */
    static Interval single_point(Expr e) {
        return Interval(e, e);
    }

    /** Is the interval the empty set */
    bool is_empty() const {
        return min.same_as(pos_inf()) || max.same_as(neg_inf());
    }

    /** Is the interval the entire range */
    bool is_everything() const {
        return min.same_as(neg_inf()) && max.same_as(pos_inf());
    }

    /** Is the interval just a single value (min == max) */
    bool is_single_point() const {
        return min.same_as(max);
    }

    /** Is the interval a particular single value */
    bool is_single_point(Expr e) const {
        return min.same_as(e) && max.same_as(e);
    }

    /** Does the interval have a finite least upper bound */
    bool has_upper_bound() const {
        return !max.same_as(pos_inf()) && !is_empty();
    }

    /** Does the interval have a finite greatest lower bound */
    bool has_lower_bound() const {
        return !min.same_as(neg_inf()) && !is_empty();
    }

    /** Does the interval have a finite upper and lower bound */
    bool is_bounded() const {
        return has_upper_bound() && has_lower_bound();
    }

    /** Is the interval the same as another interval */
    bool same_as(const Interval &other) {
        return min.same_as(other.min) && max.same_as(other.max);
    }

    /** Expand the interval to include another Interval */
    void include(const Interval &i);

    /** Expand the interval to include an Expr */
    void include(Expr e);

    /** Construct the smallest interval containing two intervals. */
    static Interval make_union(const Interval &a, const Interval &b);

    /** Construct the largest interval contained within two intervals. */
    static Interval make_intersection(const Interval &a, const Interval &b);

    /** An eagerly-simplifying max of two Exprs that respects infinities. */
    static Expr make_max(const Expr &a, const Expr &b);

    /** An eagerly-simplifying min of two Exprs that respects infinities. */
    static Expr make_min(const Expr &a, const Expr &b);

    bool operator==(const Interval &other) const {
        return (min.same_as(other.min)) && (max.same_as(other.max));
    }

private:
    static Expr neg_inf_expr, pos_inf_expr;

    // Never used inside libHalide; provided for Halide tests, to avoid needing to export
    // data fields in some build environments.
    static Expr pos_inf_noinline();
    static Expr neg_inf_noinline();
};

}  // namespace Internal
}  // namespace Halide

#endif
