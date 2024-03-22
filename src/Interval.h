#ifndef HALIDE_INTERVAL_H
#define HALIDE_INTERVAL_H

/** \file
 * Defines the Interval class
 */

#include "Expr.h"

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
    Interval(const Expr &min, const Expr &max)
        : min(min), max(max) {
        internal_assert(min.defined() && max.defined());
    }

    /** The interval representing everything. */
    static Interval everything();

    /** The interval representing nothing. */
    static Interval nothing();

    /** Construct an interval representing a single point */
    static Interval single_point(const Expr &e);

    /** Is the interval the empty set */
    bool is_empty() const;

    /** Is the interval the entire range */
    bool is_everything() const;

    /** Is the interval just a single value (min == max) */
    bool is_single_point() const;

    /** Is the interval a particular single value */
    bool is_single_point(const Expr &e) const;

    /** Does the interval have a finite least upper bound */
    bool has_upper_bound() const;

    /** Does the interval have a finite greatest lower bound */
    bool has_lower_bound() const;

    /** Does the interval have a finite upper and lower bound */
    bool is_bounded() const;

    /** Is the interval the same as another interval */
    bool same_as(const Interval &other) const;

    /** Expand the interval to include another Interval */
    void include(const Interval &i);

    /** Expand the interval to include an Expr */
    void include(const Expr &e);

    /** Construct the smallest interval containing two intervals. */
    static Interval make_union(const Interval &a, const Interval &b);

    /** Construct the largest interval contained within two other intervals. */
    static Interval make_intersection(const Interval &a, const Interval &b);

    /** An eagerly-simplifying max of two Exprs that respects infinities. */
    static Expr make_max(const Expr &a, const Expr &b);

    /** An eagerly-simplifying min of two Exprs that respects infinities. */
    static Expr make_min(const Expr &a, const Expr &b);

    /** Equivalent to same_as. Exists so that the autoscheduler can
     * compare two map<string, Interval> for equality in order to
     * cache computations. */
    bool operator==(const Interval &other) const;

private:
    static Expr neg_inf_expr, pos_inf_expr;

    // Never used inside libHalide; provided for Halide tests, to avoid needing to export
    // data fields in some build environments.
    static Expr pos_inf_noinline();
    static Expr neg_inf_noinline();
};

/** A class to represent ranges of integers. Can be unbounded above or below,
 * but they cannot be empty. */
struct ConstantInterval {
    /** The lower and upper bound of the interval. They are included
     * in the interval. */
    int64_t min = 0, max = 0;
    bool min_defined = false, max_defined = false;

    /* A default-constructed Interval is everything */
    ConstantInterval();

    /** Construct an interval from a lower and upper bound. */
    ConstantInterval(int64_t min, int64_t max);

    /** The interval representing everything. */
    static ConstantInterval everything();

    /** Construct an interval representing a single point. */
    static ConstantInterval single_point(int64_t x);

    /** Construct intervals bounded above or below. */
    static ConstantInterval bounded_below(int64_t min);
    static ConstantInterval bounded_above(int64_t max);

    /** Is the interval the entire range */
    bool is_everything() const;

    /** Is the interval just a single value (min == max) */
    bool is_single_point() const;

    /** Is the interval a particular single value */
    bool is_single_point(int64_t x) const;

    /** Does the interval have a finite least upper bound */
    bool has_upper_bound() const;

    /** Does the interval have a finite greatest lower bound */
    bool has_lower_bound() const;

    /** Does the interval have a finite upper and lower bound */
    bool is_bounded() const;

    /** Expand the interval to include another Interval */
    void include(const ConstantInterval &i);

    /** Expand the interval to include a point */
    void include(int64_t x);

    /** Test if the interval contains a particular value */
    bool contains(int64_t x) const;

    /** Test if the interval lies with a particular type. */
    bool within(Type t) const;

    /** Construct the smallest interval containing two intervals. */
    static ConstantInterval make_union(const ConstantInterval &a, const ConstantInterval &b);

    /** Equivalent to same_as. Exists so that the autoscheduler can
     * compare two map<string, Interval> for equality in order to
     * cache computations. */
    bool operator==(const ConstantInterval &other) const;

    /** In-place versions of the arithmetic operators below. */
    // @{
    void operator+=(const ConstantInterval &other);
    void operator-=(const ConstantInterval &other);
    void operator*=(const ConstantInterval &other);
    void operator/=(const ConstantInterval &other);
    // @}

    /** Negate an interval. */
    ConstantInterval operator-() const;

    /** Track what happens if a constant integer interval is forced to fit into
     * a concrete integer type. */
    void cast_to(Type t);

    /** Get constant integer bounds on a type. */
    static ConstantInterval bounds_of_type(Type);
};

/** Arithmetic operators on ConstantIntervals. The resulting interval contains
 * all possible values of the operator applied to any two elements of the
 * argument intervals. Note that these operator on unbounded integers. If you
 * are applying this to concrete small integer types, you will need to manually
 * cast the constant interval back to the desired type to model the effect of
 * overflow. */
// @{
ConstantInterval operator+(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator-(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator/(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator*(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval min(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval max(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval abs(const ConstantInterval &a);
ConstantInterval operator<<(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator>>(const ConstantInterval &a, const ConstantInterval &b);
// @}
}  // namespace Internal

/** Cast operators for ConstantIntervals. These ones have to live out in
 * Halide::, to avoid C++ name lookup confusion with the Halide::cast variants
 * that take Exprs. */
// @{
Internal::ConstantInterval cast(Type t, const Internal::ConstantInterval &a);
Internal::ConstantInterval saturating_cast(Type t, const Internal::ConstantInterval &a);
// @}

}  // namespace Halide

#endif
