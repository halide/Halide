#include "ConstantInterval.h"

#include "Error.h"
#include "IROperator.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

ConstantInterval::ConstantInterval(int64_t min, int64_t max)
    : min(min), max(max), min_defined(true), max_defined(true) {
    internal_assert(min <= max);
}

ConstantInterval ConstantInterval::everything() {
    return ConstantInterval();
}

ConstantInterval ConstantInterval::single_point(int64_t x) {
    return ConstantInterval(x, x);
}

ConstantInterval ConstantInterval::bounded_below(int64_t min) {
    ConstantInterval result(min, min);
    result.max_defined = false;
    result.max = 0;
    return result;
}

ConstantInterval ConstantInterval::bounded_above(int64_t max) {
    ConstantInterval result(max, max);
    result.min_defined = false;
    result.min = 0;
    return result;
}

bool ConstantInterval::is_everything() const {
    return !min_defined && !max_defined;
}

bool ConstantInterval::is_single_point() const {
    return min_defined && max_defined && min == max;
}

bool ConstantInterval::is_single_point(int64_t x) const {
    return min_defined && max_defined && min == x && max == x;
}

bool ConstantInterval::is_bounded() const {
    return max_defined && min_defined;
}

bool ConstantInterval::operator==(const ConstantInterval &other) const {
    if (min_defined != other.min_defined || max_defined != other.max_defined) {
        return false;
    }
    return (!min_defined || min == other.min) && (!max_defined || max == other.max);
}

void ConstantInterval::include(const ConstantInterval &i) {
    if (max_defined && i.max_defined) {
        max = std::max(max, i.max);
    } else {
        max_defined = false;
    }
    if (min_defined && i.min_defined) {
        min = std::min(min, i.min);
    } else {
        min_defined = false;
    }
}

void ConstantInterval::include(int64_t x) {
    if (max_defined) {
        max = std::max(max, x);
    }
    if (min_defined) {
        min = std::min(min, x);
    }
}

bool ConstantInterval::contains(int64_t x) const {
    const bool too_small = min_defined && x < min;
    const bool too_large = max_defined && x > max;
    return !(too_small || too_large);
}

bool ConstantInterval::contains(int32_t x) const {
    return contains((int64_t)x);
}

bool ConstantInterval::contains(uint64_t x) const {
    if (x <= (uint64_t)std::numeric_limits<int64_t>::max()) {
        // Representable as an int64_t, so just defer to that method.
        return contains((int64_t)x);
    } else {
        // This uint64_t is not representable as an int64_t, which means it's
        // greater than 2^32 - 1. Given that we can't represent that as a bound,
        // the best we can do is checking if the interval is unbounded above.
        return !max_defined;
    }
}

ConstantInterval ConstantInterval::make_union(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    result.include(b);
    return result;
}

ConstantInterval ConstantInterval::make_intersection(const ConstantInterval &a,
                                                     const ConstantInterval &b) {
    ConstantInterval result;
    if (a.min_defined) {
        if (b.min_defined) {
            result.min = std::max(a.min, b.min);
        } else {
            result.min = a.min;
        }
        result.min_defined = true;
    } else {
        result.min_defined = b.min_defined;
        result.min = b.min;
    }
    if (a.max_defined) {
        if (b.max_defined) {
            result.max = std::min(a.max, b.max);
        } else {
            result.max = a.max;
        }
        result.max_defined = true;
    } else {
        result.max_defined = b.max_defined;
        result.max = b.max;
    }
    // Our class invariant is that whenever they're both defined, min <=
    // max. Intersection is the only method that could break that, and it
    // happens when the intersected intervals do not overlap.
    internal_assert(!result.is_bounded() || result.min <= result.max)
        << "Empty ConstantInterval constructed in make_intersection";
    return result;
}

void ConstantInterval::operator+=(const ConstantInterval &other) {
    (*this) = (*this) + other;
}

void ConstantInterval::operator-=(const ConstantInterval &other) {
    (*this) = (*this) - other;
}

void ConstantInterval::operator*=(const ConstantInterval &other) {
    (*this) = (*this) * other;
}

void ConstantInterval::operator/=(const ConstantInterval &other) {
    (*this) = (*this) / other;
}

void ConstantInterval::operator%=(const ConstantInterval &other) {
    (*this) = (*this) % other;
}

void ConstantInterval::operator+=(int64_t x) {
    (*this) = (*this) + x;
}

void ConstantInterval::operator-=(int64_t x) {
    (*this) = (*this) - x;
}

void ConstantInterval::operator*=(int64_t x) {
    (*this) = (*this) * x;
}

void ConstantInterval::operator/=(int64_t x) {
    (*this) = (*this) / x;
}

void ConstantInterval::operator%=(int64_t x) {
    (*this) = (*this) % x;
}

bool operator<=(const ConstantInterval &a, const ConstantInterval &b) {
    return a.max_defined && b.min_defined && a.max <= b.min;
}
bool operator<(const ConstantInterval &a, const ConstantInterval &b) {
    return a.max_defined && b.min_defined && a.max < b.min;
}

bool operator<=(const ConstantInterval &a, int64_t b) {
    return a.max_defined && a.max <= b;
}
bool operator<(const ConstantInterval &a, int64_t b) {
    return a.max_defined && a.max < b;
}

bool operator<=(int64_t a, const ConstantInterval &b) {
    return b.min_defined && a <= b.min;
}
bool operator<(int64_t a, const ConstantInterval &b) {
    return b.min_defined && a < b.min;
}

void ConstantInterval::cast_to(const Type &t) {
    if (!t.can_represent(*this)) {
        // We have potential overflow or underflow, return the entire bounds of
        // the type.
        ConstantInterval type_bounds;
        if (t.is_int()) {
            if (t.bits() <= 64) {
                type_bounds.min_defined = type_bounds.max_defined = true;
                type_bounds.min = ((int64_t)(-1)) << (t.bits() - 1);
                type_bounds.max = ~type_bounds.min;
            }
        } else if (t.is_uint()) {
            type_bounds.min_defined = true;
            type_bounds.min = 0;
            if (t.bits() < 64) {
                type_bounds.max_defined = true;
                type_bounds.max = (((int64_t)(1)) << t.bits()) - 1;
            }
        }
        // If it's not int or uint, we're setting this to a default-constructed
        // ConstantInterval, which is everything.
        *this = type_bounds;
    }
}

ConstantInterval ConstantInterval::operator-() const {
    ConstantInterval result;
    if (min_defined && min != INT64_MIN) {
        result.max_defined = true;
        result.max = -min;
    }
    if (max_defined) {
        result.min_defined = true;
        result.min = -max;
    }
    return result;
}

ConstantInterval ConstantInterval::bounds_of_type(Type t) {
    return cast(t, ConstantInterval::everything());
}

ConstantInterval operator+(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;
    result.min_defined = a.min_defined &&
                         b.min_defined &&
                         add_with_overflow(64, a.min, b.min, &result.min);

    result.max_defined = a.max_defined &&
                         b.max_defined &&
                         add_with_overflow(64, a.max, b.max, &result.max);
    return result;
}

ConstantInterval operator-(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;
    result.min_defined = a.min_defined &&
                         b.max_defined &&
                         sub_with_overflow(64, a.min, b.max, &result.min);
    result.max_defined = a.max_defined &&
                         b.min_defined &&
                         sub_with_overflow(64, a.max, b.min, &result.max);
    return result;
}

ConstantInterval operator/(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;

    result.min = INT64_MAX;
    result.max = INT64_MIN;

    auto consider_case = [&](int64_t a, int64_t b) {
        int64_t v = div_imp(a, b);
        result.min = std::min(result.min, v);
        result.max = std::max(result.max, v);
    };

    // Enumerate all possible values for the min and max and take the extreme values.
    if (a.min_defined && b.min_defined && b.min != 0) {
        consider_case(a.min, b.min);
    }

    if (a.min_defined && b.max_defined && b.max != 0) {
        consider_case(a.min, b.max);
    }

    if (a.max_defined && b.max_defined && b.max != 0) {
        consider_case(a.max, b.max);
    }

    if (a.max_defined && b.min_defined && b.min != 0) {
        consider_case(a.max, b.min);
    }

    // Define an int64_t zero just to pacify std::min and std::max
    constexpr int64_t zero = 0;

    const bool b_positive = b > 0;
    const bool b_negative = b < 0;
    if ((b_positive && !b.max_defined) ||
        (b_negative && !b.min_defined)) {
        // Take limit as other -> +/- infinity
        result.min = std::min(result.min, zero);
        result.max = std::max(result.max, zero);
    }

    result.min_defined = ((a.min_defined && b_positive) ||
                          (a.max_defined && b_negative));
    result.max_defined = ((a.max_defined && b_positive) ||
                          (a.min_defined && b_negative));

    // That's as far as we can get knowing the sign of the
    // denominator. For bounded numerators, we additionally know
    // that div can't make anything larger in magnitude, so we can
    // take the intersection with that.
    if (a.is_bounded() && a.min != INT64_MIN) {
        int64_t magnitude = std::max(a.max, -a.min);
        if (result.min_defined) {
            result.min = std::max(result.min, -magnitude);
        } else {
            result.min = -magnitude;
        }
        if (result.max_defined) {
            result.max = std::min(result.max, magnitude);
        } else {
            result.max = magnitude;
        }
        result.min_defined = result.max_defined = true;
    }

    // Finally we can deduce the sign if the numerator and denominator are
    // non-positive or non-negative.
    bool a_non_negative = a >= 0;
    bool b_non_negative = b >= 0;
    bool a_non_positive = a <= 0;
    bool b_non_positive = b <= 0;
    if ((a_non_negative && b_non_negative) ||
        (a_non_positive && b_non_positive)) {
        if (result.min_defined) {
            result.min = std::max(result.min, zero);
        } else {
            result.min_defined = true;
            result.min = 0;
        }
    } else if ((a_non_negative && b_non_positive) ||
               (a_non_positive && b_non_negative)) {
        if (result.max_defined) {
            result.max = std::min(result.max, zero);
        } else {
            result.max_defined = true;
            result.max = 0;
        }
    }

    // Normalize the values if it's undefined
    if (!result.min_defined) {
        result.min = 0;
    }
    if (!result.max_defined) {
        result.max = 0;
    }

    // Check the class invariant as a sanity check.
    internal_assert(!result.is_bounded() || (result.min <= result.max));

    return result;
}

ConstantInterval operator*(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;

    // Compute a possible extreme value of the product, either incorporating it
    // into result.min / result.max, or setting the min/max defined flags if it
    // overflows.
    auto consider_case = [&](int64_t a, int64_t b) {
        int64_t c;
        if (mul_with_overflow(64, a, b, &c)) {
            result.min = std::min(result.min, c);
            result.max = std::max(result.max, c);
        } else if ((a > 0) == (b > 0)) {
            result.max_defined = false;
        } else {
            result.min_defined = false;
        }
    };

    result.min_defined = result.max_defined = true;
    result.min = INT64_MAX;
    result.max = INT64_MIN;
    if (a.min_defined && b.min_defined) {
        consider_case(a.min, b.min);
    }
    if (a.min_defined && b.max_defined) {
        consider_case(a.min, b.max);
    }
    if (a.max_defined && b.min_defined) {
        consider_case(a.max, b.min);
    }
    if (a.max_defined && b.max_defined) {
        consider_case(a.max, b.max);
    }

    const bool a_bounded_negative = a.min_defined && a <= 0;
    const bool a_bounded_positive = a.max_defined && a >= 0;
    const bool b_bounded_negative = b.min_defined && b <= 0;
    const bool b_bounded_positive = b.max_defined && b >= 0;

    if (result.min_defined) {
        result.min_defined =
            ((a.is_bounded() && b.is_bounded()) ||
             (a >= 0 && b >= 0) ||
             (a <= 0 && b <= 0) ||
             (a.min_defined && b_bounded_positive) ||
             (b.min_defined && a_bounded_positive) ||
             (a.max_defined && b_bounded_negative) ||
             (b.max_defined && a_bounded_negative));
    }

    if (result.max_defined) {
        result.max_defined =
            ((a.is_bounded() && b.is_bounded()) ||
             (a >= 0 && b <= 0) ||
             (a <= 0 && b >= 0) ||
             (a.max_defined && b_bounded_positive) ||
             (b.max_defined && a_bounded_positive) ||
             (a.min_defined && b_bounded_negative) ||
             (b.min_defined && a_bounded_negative));
    }

    if (!result.min_defined) {
        result.min = 0;
    }

    if (!result.max_defined) {
        result.max = 0;
    }

    // Check the class invariant as a sanity check.
    internal_assert(!result.is_bounded() || (result.min <= result.max));

    return result;
}

ConstantInterval operator%(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;

    // Maybe the mod won't actually do anything
    if (a >= 0 && a < abs(b)) {
        return a;
    }

    // The result is at least zero.
    result.min_defined = true;
    result.min = 0;

    if (a.is_single_point() && b.is_single_point()) {
        return ConstantInterval::single_point(mod_imp(a.min, b.min));
    }

    // Mod by produces a result between 0
    // and max(0, abs(modulus) - 1). However, if b is unbounded in
    // either direction, abs(modulus) could be arbitrarily
    // large.
    if (b.is_bounded() && b.max != INT64_MIN) {
        result.max_defined = true;
        result.max = 0;                                // When b == 0
        result.max = std::max(result.max, b.max - 1);  // When b > 0
        result.max = std::max(result.max, ~b.min);     // When b < 0
        // Note that ~b.min is equal to (-1 - b.min). It's written as ~b.min to
        // make it clear that it can't overflow.
    }

    // If a is positive, mod can't make it larger
    if (a.is_bounded() && a.min >= 0) {
        if (result.max_defined) {
            result.max = std::min(result.max, a.max);
        } else {
            result.max_defined = true;
            result.max = a.max;
        }
    }

    // Check the class invariant as a sanity check.
    internal_assert(!result.is_bounded() || (result.min <= result.max));

    return result;
}

ConstantInterval operator+(const ConstantInterval &a, int64_t b) {
    return a + ConstantInterval::single_point(b);
}

ConstantInterval operator-(const ConstantInterval &a, int64_t b) {
    return a - ConstantInterval::single_point(b);
}

ConstantInterval operator/(const ConstantInterval &a, int64_t b) {
    return a / ConstantInterval::single_point(b);
}

ConstantInterval operator*(const ConstantInterval &a, int64_t b) {
    return a * ConstantInterval::single_point(b);
}

ConstantInterval operator%(const ConstantInterval &a, int64_t b) {
    return a % ConstantInterval::single_point(b);
}

ConstantInterval min(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;
    result.max_defined = a.max_defined || b.max_defined;
    result.min_defined = a.min_defined && b.min_defined;
    if (a.max_defined && b.max_defined) {
        result.max = std::min(a.max, b.max);
    } else if (a.max_defined) {
        result.max = a.max;
    } else if (b.max_defined) {
        result.max = b.max;
    }
    if (a.min_defined && b.min_defined) {
        result.min = std::min(a.min, b.min);
    }
    return result;
}

ConstantInterval min(const ConstantInterval &a, int64_t b) {
    ConstantInterval result = a;
    if (result.max_defined) {
        result.max = std::min(a.max, b);
    } else {
        result.max = b;
        result.max_defined = true;
    }
    if (result.min_defined) {
        result.min = std::min(a.min, b);
    }
    return result;
}

ConstantInterval max(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;
    result.min_defined = a.min_defined || b.min_defined;
    result.max_defined = a.max_defined && b.max_defined;
    if (a.min_defined && b.min_defined) {
        result.min = std::max(a.min, b.min);
    } else if (a.min_defined) {
        result.min = a.min;
    } else if (b.min_defined) {
        result.min = b.min;
    }
    if (a.max_defined && b.max_defined) {
        result.max = std::max(a.max, b.max);
    }
    return result;
}

ConstantInterval max(const ConstantInterval &a, int64_t b) {
    ConstantInterval result = a;
    if (result.min_defined) {
        result.min = std::max(a.min, b);
    } else {
        result.min = b;
        result.min_defined = true;
    }
    if (result.max_defined) {
        result.max = std::max(a.max, b);
    }
    return result;
}

ConstantInterval abs(const ConstantInterval &a) {
    ConstantInterval result;
    if (a.min_defined && a.max_defined && a.min != INT64_MIN) {
        result.max_defined = true;
        result.max = std::max(-a.min, a.max);
    }
    result.min_defined = true;
    if (a.min_defined && a.min > 0) {
        result.min = a.min;
    } else if (a.max_defined && a.max < 0 && a.max != INT64_MIN) {
        result.min = -a.max;
    } else {
        result.min = 0;
    }

    return result;
}

ConstantInterval operator<<(const ConstantInterval &a, const ConstantInterval &b) {
    // In infinite integers (with no overflow):

    // a << b == a * 2^b

    // This can't be used directly, because if b is negative then 2^b is not an
    // integer. Instead, we'll break b into a difference of two positive values:
    // b = b_pos - b_neg
    // So
    // a * 2^b
    // = a * 2^(b_pos - b_neg)
    // = (a * 2^b_pos) / 2^b_neg

    // From there we can use the * and / operators.

    ConstantInterval b_pos = max(b, 0), b_neg = max(-b, 0);

    // At this point, we have sliced the interval b into two parts. E.g.
    // if b = [10, 12],  b_pos = [10, 12] and b_neg = [0, 0]
    // if b = [-4, 8],   b_pos = [0, 8]   and b_neg = [0, 4]
    // if b = [-10, -3], b_pos = [0, 0]   and b_neg = [3, 10]
    // if b = [-3, inf], b_pos = [0, inf] and b_neg = [0, 3]
    // In all cases, note that b_pos - b_neg = b by our definition of operator-
    // for ConstantIntervals above (ignoring corner cases, for which b_pos -
    // b_neg safely over-approximates the bounds of b).

    auto two_to_the = [](const ConstantInterval &i) {
        const int64_t one = 1;
        ConstantInterval r;
        // We should know i is positive at this point.
        internal_assert(i.min_defined && i.min >= 0);
        r.min_defined = true;
        if (i.min >= 63) {
            // It's at least a value too large for us to represent, which is not
            // the same as min_defined = false.
            r.min = INT64_MAX;
        } else {
            r.min = one << i.min;
        }
        if (i.max < 63) {
            r.max_defined = true;
            r.max = one << i.max;
        }
        return r;
    };

    return (a * two_to_the(b_pos)) / two_to_the(b_neg);
}

ConstantInterval operator<<(const ConstantInterval &a, int64_t b) {
    return a << ConstantInterval::single_point(b);
}

ConstantInterval operator<<(int64_t a, const ConstantInterval &b) {
    return ConstantInterval::single_point(a) << b;
}

ConstantInterval operator>>(const ConstantInterval &a, const ConstantInterval &b) {
    return a << (-b);
}

ConstantInterval operator>>(const ConstantInterval &a, int64_t b) {
    return a >> ConstantInterval::single_point(b);
}

ConstantInterval operator>>(int64_t a, const ConstantInterval &b) {
    return ConstantInterval::single_point(a) >> b;
}

}  // namespace Internal

using namespace Internal;

ConstantInterval cast(Type t, const ConstantInterval &a) {
    ConstantInterval result = a;
    result.cast_to(t);
    return result;
}

ConstantInterval saturating_cast(Type t, const ConstantInterval &a) {
    ConstantInterval b = ConstantInterval::bounds_of_type(t);
    if (a >= b) {
        return ConstantInterval::single_point(b.max);
    } else if (a <= b) {
        return ConstantInterval::single_point(b.min);
    } else {
        return ConstantInterval::make_intersection(a, b);
    }
}

}  // namespace Halide
