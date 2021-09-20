#include "Interval.h"

#include <algorithm>

#include "IR.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "Type.h"

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

ConstantInterval ConstantInterval::make_union(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result = a;
    result.include(b);
    return result;
}

}  // namespace Internal
}  // namespace Halide
