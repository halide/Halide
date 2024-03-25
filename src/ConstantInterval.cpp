#include "ConstantInterval.h"

#include "Error.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

ConstantInterval::ConstantInterval() = default;

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
    return result;
}

ConstantInterval ConstantInterval::bounded_above(int64_t max) {
    ConstantInterval result(max, max);
    result.min_defined = false;
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

bool ConstantInterval::has_upper_bound() const {
    return max_defined;
}

bool ConstantInterval::has_lower_bound() const {
    return min_defined;
}

bool ConstantInterval::is_bounded() const {
    return has_upper_bound() && has_lower_bound();
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
    return !((min_defined && x < min) ||
             (max_defined && x > max));
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
    internal_assert(!result.is_bounded() || result.min <= result.max)
        << "Empty ConstantInterval constructed in make_intersection";
    return result;
}

// TODO: These were taken directly from the simplifier, so change the simplifier
// to use these instead of duplicating the code.
void ConstantInterval::operator+=(const ConstantInterval &other) {
    min_defined = min_defined &&
                  other.min_defined &&
                  add_with_overflow(64, min, other.min, &min);
    max_defined = max_defined &&
                  other.max_defined &&
                  add_with_overflow(64, max, other.max, &max);
}

void ConstantInterval::operator-=(const ConstantInterval &other) {
    min_defined = min_defined &&
                  other.max_defined &&
                  sub_with_overflow(64, min, other.max, &min);
    max_defined = max_defined &&
                  other.min_defined &&
                  sub_with_overflow(64, max, other.min, &max);
}

void ConstantInterval::operator*=(const ConstantInterval &other) {
    ConstantInterval result;

    // Compute a possible extreme value of the product, setting the min/max
    // defined flags if it's unbounded.
    auto saturating_mul = [&](int64_t a, int64_t b) -> int64_t {
        int64_t c;
        if (mul_with_overflow(64, a, b, &c)) {
            return c;
        } else if ((a > 0) == (b > 0)) {
            result.max_defined = false;
            return INT64_MAX;
        } else {
            result.min_defined = false;
            return INT64_MIN;
        }
    };

    bool positive = min_defined && min > 0;
    bool other_positive = other.min_defined && other.min > 0;
    bool bounded = min_defined && max_defined;
    bool other_bounded = other.min_defined && other.max_defined;

    if (bounded && other_bounded) {
        // Both are bounded
        result.min_defined = result.max_defined = true;
        int64_t v1 = saturating_mul(min, other.min);
        int64_t v2 = saturating_mul(min, other.max);
        int64_t v3 = saturating_mul(max, other.min);
        int64_t v4 = saturating_mul(max, other.max);
        if (result.min_defined) {
            result.min = std::min(std::min(v1, v2), std::min(v3, v4));
        } else {
            result.min = 0;
        }
        if (result.max_defined) {
            result.max = std::max(std::max(v1, v2), std::max(v3, v4));
        } else {
            result.max = 0;
        }
    } else if ((max_defined && other_bounded && other_positive) ||
               (other.max_defined && bounded && positive)) {
        // One side has a max, and the other side is bounded and positive
        // (e.g. a constant).
        result.max = saturating_mul(max, other.max);
        if (!result.max_defined) {
            result.max = 0;
        }
    } else if ((min_defined && other_bounded && other_positive) ||
               (other.min_defined && bounded && positive)) {
        // One side has a min, and the other side is bounded and positive
        // (e.g. a constant).
        min = saturating_mul(min, other.min);
        if (!result.min_defined) {
            result.min = 0;
        }
    }
    // TODO: what about the above two cases, but for multiplication by bounded
    // and negative intervals?

    *this = result;
}

void ConstantInterval::operator/=(const ConstantInterval &other) {
    ConstantInterval result;

    result.min = INT64_MAX;
    result.max = INT64_MIN;

    // Enumerate all possible values for the min and max and take the extreme values.
    if (min_defined && other.min_defined && other.min != 0) {
        int64_t v = div_imp(min, other.min);
        result.min = std::min(result.min, v);
        result.max = std::max(result.max, v);
    }

    if (min_defined && other.max_defined && other.max != 0) {
        int64_t v = div_imp(min, other.max);
        result.min = std::min(result.min, v);
        result.max = std::max(result.max, v);
    }

    if (max_defined && other.max_defined && other.max != 0) {
        int64_t v = div_imp(max, other.max);
        result.min = std::min(result.min, v);
        result.max = std::max(result.max, v);
    }

    if (max_defined && other.min_defined && other.min != 0) {
        int64_t v = div_imp(max, other.min);
        result.min = std::min(result.min, v);
        result.max = std::max(result.max, v);
    }

    // Define an int64_t zero just to pacify std::min and std::max
    constexpr int64_t zero = 0;

    const bool other_positive = other.min_defined && other.min > 0;
    const bool other_negative = other.max_defined && other.max < 0;
    if ((other_positive && !other.max_defined) ||
        (other_negative && !other.min_defined)) {
        // Take limit as other -> +/- infinity
        result.min = std::min(result.min, zero);
        result.max = std::max(result.max, zero);
    }

    bool bounded_numerator = min_defined && max_defined;

    result.min_defined = ((min_defined && other_positive) ||
                          (max_defined && other_negative));
    result.max_defined = ((max_defined && other_positive) ||
                          (min_defined && other_negative));

    // That's as far as we can get knowing the sign of the
    // denominator. For bounded numerators, we additionally know
    // that div can't make anything larger in magnitude, so we can
    // take the intersection with that.
    if (bounded_numerator && min != INT64_MIN) {
        int64_t magnitude = std::max(max, -min);
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

    // Finally we can provide a bound if the numerator and denominator are
    // non-positive or non-negative.
    bool numerator_non_negative = min_defined && min >= 0;
    bool denominator_non_negative = other.min_defined && other.min >= 0;
    bool numerator_non_positive = max_defined && max <= 0;
    bool denominator_non_positive = other.max_defined && other.max <= 0;
    if ((numerator_non_negative && denominator_non_negative) ||
        (numerator_non_positive && denominator_non_positive)) {
        if (result.min_defined) {
            result.min = std::max(result.min, zero);
        } else {
            result.min_defined = true;
            result.min = 0;
        }
    }
    if ((numerator_non_negative && denominator_non_positive) ||
        (numerator_non_positive && denominator_non_negative)) {
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

    *this = result;
}

void ConstantInterval::operator+=(int64_t x) {
    // TODO: Optimize this
    *this += ConstantInterval(x, x);
}

void ConstantInterval::operator-=(int64_t x) {
    // TODO: Optimize this
    *this -= ConstantInterval(x, x);
}

void ConstantInterval::operator*=(int64_t x) {
    // TODO: Optimize this
    *this *= ConstantInterval(x, x);
}

void ConstantInterval::operator/=(int64_t x) {
    // TODO: Optimize this
    *this /= ConstantInterval(x, x);
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
    ConstantInterval result = a;
    result += b;
    return result;
}

ConstantInterval operator-(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    result -= b;
    return result;
}

ConstantInterval operator/(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    result /= b;
    return result;
}

ConstantInterval operator*(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    result *= b;
    return result;
}

ConstantInterval operator%(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;

    // Maybe the mod won't actually do anything
    if (a >= 0 && a < b) {
        return a;
    }

    // The result is at least zero.
    result.min_defined = true;
    result.min = 0;

    // Mod by produces a result between 0
    // and max(0, abs(modulus) - 1). However, if b is unbounded in
    // either direction, abs(modulus) could be arbitrarily
    // large.
    if (b.is_bounded()) {
        result.max_defined = true;
        result.max = 0;                                 // When b == 0
        result.max = std::max(result.max, b.max - 1);   // When b > 0
        result.max = std::max(result.max, -1 - b.min);  // When b < 0
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

    return result;
}

ConstantInterval operator+(const ConstantInterval &a, int64_t b) {
    return a + ConstantInterval(b, b);
}

ConstantInterval operator-(const ConstantInterval &a, int64_t b) {
    return a - ConstantInterval(b, b);
}

ConstantInterval operator/(const ConstantInterval &a, int64_t b) {
    return a / ConstantInterval(b, b);
}

ConstantInterval operator*(const ConstantInterval &a, int64_t b) {
    return a * ConstantInterval(b, b);
}

ConstantInterval operator%(const ConstantInterval &a, int64_t b) {
    return a * ConstantInterval(b, b);
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

ConstantInterval abs(const ConstantInterval &a) {
    ConstantInterval result;
    if (a.min_defined && a.max_defined && a.min != INT64_MIN) {
        result.max_defined = true;
        result.max = std::max(-a.min, a.max);
    }
    result.min_defined = true;
    if (a.min_defined && a.min > 0) {
        result.min = a.min;
    } else {
        result.min = 0;
    }

    return result;
}

ConstantInterval operator<<(const ConstantInterval &a, const ConstantInterval &b) {
    // Try to map this to a multiplication and a division
    ConstantInterval mul, div;
    constexpr int64_t one = 1;
    if (b.min_defined) {
        if (b.min >= 0 && b.min < 63) {
            mul.min = one << b.min;
            mul.min_defined = true;
            div.max = one;
            div.max_defined = true;
        } else if (b.min > -63 && b.min <= 0) {
            mul.min = one;
            mul.min_defined = true;
            div.max = one << (-b.min);
            div.max_defined = true;
        }
    }
    if (b.max_defined) {
        if (b.max >= 0 && b.max < 63) {
            mul.max = one << b.max;
            mul.max_defined = true;
            div.min = one;
            div.min_defined = true;
        } else if (b.max > -63 && b.max <= 0) {
            mul.max = one;
            mul.max_defined = true;
            div.min = one << (-b.max);
            div.min_defined = true;
        }
    }
    return (a * mul) / div;
}

ConstantInterval operator>>(const ConstantInterval &a, const ConstantInterval &b) {
    return a << (-b);
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

    if (b.max_defined && a.min_defined && a.min > b.max) {
        return ConstantInterval(b.max, b.max);
    } else if (b.min_defined && a.max_defined && a.max < b.min) {
        return ConstantInterval(b.min, b.min);
    }

    ConstantInterval result = a;
    result.max_defined = a.max_defined || b.max_defined;
    if (a.max_defined) {
        if (b.max_defined) {
            result.max = std::min(a.max, b.max);
        } else {
            result.max = a.max;
        }
    } else if (b.max_defined) {
        result.max = b.max;
    }
    result.min_defined = a.min_defined || b.min_defined;
    if (a.min_defined) {
        if (b.min_defined) {
            result.min = std::max(a.min, b.min);
        } else {
            result.min = a.min;
        }
    } else if (b.min_defined) {
        result.min = b.min;
    }
    return result;
}

}  // namespace Halide
