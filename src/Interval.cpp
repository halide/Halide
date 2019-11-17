#include "Interval.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

IRMatcher::Wild<0> x;
IRMatcher::Wild<1> y;
IRMatcher::WildConst<0> c0;
IRMatcher::WildConst<1> c1;

Expr make_max_helper(const Expr &a, const Expr &b) {
    Scope<Expr> scope;
    auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), a.type(), scope);

    if (rewrite(max(x, x), x) ||
        rewrite(max(x, Interval::pos_inf), Interval::pos_inf) ||
        rewrite(max(Interval::pos_inf, x), Interval::pos_inf) ||
        rewrite(max(x, Interval::neg_inf), x) ||
        rewrite(max(Interval::neg_inf, x), x) ||
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
    Scope<Expr> scope;
    auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), a.type(), scope);

    if (rewrite(min(x, x), x) ||
        rewrite(min(x, Interval::pos_inf), x) ||
        rewrite(min(Interval::pos_inf, x), x) ||
        rewrite(min(x, Interval::neg_inf), Interval::neg_inf) ||
        rewrite(min(Interval::neg_inf, x), Interval::neg_inf) ||
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

void Interval::include(Expr e) {
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
Expr Interval::pos_inf = Variable::make(Handle(), "pos_inf");
Expr Interval::neg_inf = Variable::make(Handle(), "neg_inf");

}  // namespace Internal
}  // namespace Halide
