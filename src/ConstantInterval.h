#ifndef HALIDE_CONSTANT_INTERVAL_H
#define HALIDE_CONSTANT_INTERVAL_H

#include <stdint.h>

/** \file
 * Defines the ConstantInterval class, and operators on it.
 */

namespace Halide {

struct Type;

namespace Internal {

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

    /** Does the interval have a finite upper and lower bound */
    bool is_bounded() const;

    /** Expand the interval to include another Interval */
    void include(const ConstantInterval &i);

    /** Expand the interval to include a point */
    void include(int64_t x);

    /** Test if the interval contains a particular value */
    bool contains(int64_t x) const;

    /** Construct the smallest interval containing two intervals. */
    static ConstantInterval make_union(const ConstantInterval &a, const ConstantInterval &b);

    /** Construct the largest interval contained within two intervals. Throws an
     * error if the interval is empty. */
    static ConstantInterval make_intersection(const ConstantInterval &a, const ConstantInterval &b);

    /** Equivalent to same_as. Exists so that the autoscheduler can
     * compare two map<string, Interval> for equality in order to
     * cache computations. */
    bool operator==(const ConstantInterval &other) const;

    /** In-place versions of the arithmetic operators below. */
    // @{
    void operator+=(const ConstantInterval &other);
    void operator+=(int64_t);
    void operator-=(const ConstantInterval &other);
    void operator-=(int64_t);
    void operator*=(const ConstantInterval &other);
    void operator*=(int64_t);
    void operator/=(const ConstantInterval &other);
    void operator/=(int64_t);
    void operator%=(const ConstantInterval &other);
    void operator%=(int64_t);
    // @}

    /** Negate an interval. */
    ConstantInterval operator-() const;

    /** Track what happens if a constant integer interval is forced to fit into
     * a concrete integer type. */
    void cast_to(const Type &t);

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
ConstantInterval operator+(const ConstantInterval &a, int64_t b);
ConstantInterval operator-(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator-(const ConstantInterval &a, int64_t b);
ConstantInterval operator/(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator/(const ConstantInterval &a, int64_t b);
ConstantInterval operator*(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator*(const ConstantInterval &a, int64_t b);
ConstantInterval operator%(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator%(const ConstantInterval &a, int64_t b);
ConstantInterval min(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval min(const ConstantInterval &a, int64_t b);
ConstantInterval max(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval max(const ConstantInterval &a, int64_t b);
ConstantInterval abs(const ConstantInterval &a);
ConstantInterval operator<<(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator<<(const ConstantInterval &a, int64_t b);
ConstantInterval operator>>(const ConstantInterval &a, const ConstantInterval &b);
ConstantInterval operator>>(const ConstantInterval &a, int64_t b);
// @}

/** Comparison operators on ConstantIntervals. Returns whether the comparison is
 * true for all values of the two intervals. */
// @{
bool operator<=(const ConstantInterval &a, const ConstantInterval &b);
bool operator<=(const ConstantInterval &a, int64_t b);
bool operator<=(int64_t a, const ConstantInterval &b);
bool operator<(const ConstantInterval &a, const ConstantInterval &b);
bool operator<(const ConstantInterval &a, int64_t b);
bool operator<(int64_t a, const ConstantInterval &b);

inline bool operator>=(const ConstantInterval &a, const ConstantInterval &b) {
    return b <= a;
}
inline bool operator>(const ConstantInterval &a, const ConstantInterval &b) {
    return b < a;
}
inline bool operator>=(const ConstantInterval &a, int64_t b) {
    return b <= a;
}
inline bool operator>(const ConstantInterval &a, int64_t b) {
    return b < a;
}
inline bool operator>=(int64_t a, const ConstantInterval &b) {
    return b <= a;
}
inline bool operator>(int64_t a, const ConstantInterval &b) {
    return b < a;
}

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
