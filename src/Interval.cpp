#include "Interval.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IROperator.h"

using namespace Halide::Internal;

namespace Halide {
namespace Internal {

namespace {

IRMatcher::Wild<0> x;
IRMatcher::Wild<1> y;
IRMatcher::WildConst<0> c0;
IRMatcher::WildConst<1> c1;

Expr make_max_helper(const Expr &a, const Expr &b) {
    auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), a.type());

    Expr pos_inf = Interval::pos_inf();
    Expr neg_inf = Interval::neg_inf();
    if (rewrite(max(x, x), x) ||
        rewrite(max(x, pos_inf), pos_inf) ||
        rewrite(max(pos_inf, x), pos_inf) ||
        rewrite(max(x, neg_inf), x) ||
        rewrite(max(neg_inf, x), x) ||
        rewrite(max(c0, c1), fold(max(c0, c1))) ||
        rewrite(max(c0, x), max(x, c0)) ||
        rewrite(max(max(x, c0), c1), max(x, fold(max(c0, c1)))) ||
        rewrite(max(max(x, c0), y), max(max(x, y), c0)) ||
        rewrite(max(x, max(y, c0)), max(max(x, y), c0)) ||
        rewrite(max(max(x, y), x), a) ||
        rewrite(max(max(x, y), y), a) ||
        rewrite(max(x, max(x, y)), b) ||
        rewrite(max(y, max(x, y)), b)) {
        return rewrite.result;
    } else {
        return max(a, b);
    }
}

Expr make_min_helper(const Expr &a, const Expr &b) {
    auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), a.type());

    Expr pos_inf = Interval::pos_inf();
    Expr neg_inf = Interval::neg_inf();
    if (rewrite(min(x, x), x) ||
        rewrite(min(x, pos_inf), x) ||
        rewrite(min(pos_inf, x), x) ||
        rewrite(min(x, neg_inf), neg_inf) ||
        rewrite(min(neg_inf, x), neg_inf) ||
        rewrite(min(c0, c1), fold(min(c0, c1))) ||
        rewrite(min(c0, x), min(x, c0)) ||
        rewrite(min(min(x, c0), c1), min(x, fold(min(c0, c1)))) ||
        rewrite(min(min(x, c0), y), min(min(x, y), c0)) ||
        rewrite(min(x, min(y, c0)), min(min(x, y), c0)) ||
        rewrite(min(min(x, y), x), a) ||
        rewrite(min(min(x, y), y), a) ||
        rewrite(min(x, min(x, y)), b) ||
        rewrite(min(y, min(x, y)), b)) {
        return rewrite.result;
    } else {
        return min(a, b);
    }
}

}  // namespace

Interval Interval::everything() {
    return Interval(neg_inf(), pos_inf());
}

Interval Interval::nothing() {
    return Interval(pos_inf(), neg_inf());
}

Interval Interval::single_point(const Expr &e) {
    return Interval(e, e);
}

bool Interval::is_empty() const {
    return min.same_as(pos_inf()) || max.same_as(neg_inf());
}

bool Interval::is_everything() const {
    return min.same_as(neg_inf()) && max.same_as(pos_inf());
}

bool Interval::is_single_point() const {
    return min.same_as(max);
}

bool Interval::is_single_point(const Expr &e) const {
    return min.same_as(e) && max.same_as(e);
}

bool Interval::has_upper_bound() const {
    return !max.same_as(pos_inf()) && !is_empty();
}

bool Interval::has_lower_bound() const {
    return !min.same_as(neg_inf()) && !is_empty();
}

bool Interval::is_bounded() const {
    return has_upper_bound() && has_lower_bound();
}

bool Interval::same_as(const Interval &other) const {
    return min.same_as(other.min) && max.same_as(other.max);
}

bool Interval::operator==(const Interval &other) const {
    return (min.same_as(other.min)) && (max.same_as(other.max));
}

// This is called repeatedly by bounds inference and the solver to
// build large expressions, so we want to simplify eagerly to avoid
// monster expressions.
Expr Interval::make_max(const Expr &a, const Expr &b) {
    return make_max_helper(a, b);
}

Expr Interval::make_min(const Expr &a, const Expr &b) {
    return make_min_helper(a, b);
}

void Interval::include(const Interval &i) {
    max = Interval::make_max(max, i.max);
    min = Interval::make_min(min, i.min);
}

void Interval::include(const Expr &e) {
    max = Interval::make_max(max, e);
    min = Interval::make_min(min, e);
}

Interval Interval::make_union(const Interval &a, const Interval &b) {
    Interval result = a;
    result.include(b);
    return result;
}

Interval Interval::make_intersection(const Interval &a, const Interval &b) {
    return Interval(Interval::make_max(a.min, b.min),
                    Interval::make_min(a.max, b.max));
}

// Use Handle types for positive and negative infinity, to prevent
// accidentally doing arithmetic on them.
Expr Interval::pos_inf_expr = Variable::make(Handle(), "pos_inf");
Expr Interval::neg_inf_expr = Variable::make(Handle(), "neg_inf");

Expr Interval::pos_inf_noinline() {
    return Interval::pos_inf_expr;
}
Expr Interval::neg_inf_noinline() {
    return Interval::neg_inf_expr;
}

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

void ConstantInterval::cast_to(Type t) {
    if (!(max_defined && t.can_represent(max) &&
          min_defined && t.can_represent(min))) {
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

ConstantInterval min(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    if (a.min_defined && b.min_defined && b.min < a.min) {
        result.min = b.min;
    }
    if (a.max_defined && b.max_defined && b.max < a.max) {
        result.max = b.max;
    }
    return result;
}

ConstantInterval max(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    if (a.min_defined && b.min_defined && b.min > a.min) {
        result.min = b.min;
    }
    if (a.max_defined && b.max_defined && b.max > a.max) {
        result.max = b.max;
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

}  // namespace Internal

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
