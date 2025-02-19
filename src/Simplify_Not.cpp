#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Not *op, ExprInfo *info) {
    ExprInfo a_info;
    Expr a = mutate(op->a, &a_info);

    if (info) {
        info->bounds = ConstantInterval::single_point(1) - a_info.bounds;
        info->alignment = ModulusRemainder{0, 1} - a_info.alignment;
        info->cast_to(op->type);
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::not_op(a), op->type);

    if (rewrite(!c0, fold(!c0)) ||
        rewrite(!(x < y), y <= x) ||
        rewrite(!(x <= y), y < x) ||
        rewrite(!(x == y), x != y) ||
        rewrite(!(x != y), x == y) ||
        rewrite(!!x, x)) {
        return rewrite.result;
    }

    if (rewrite(!broadcast(x, c0), broadcast(!x, c0)) ||
        rewrite(!likely(x), likely(!x)) ||
        rewrite(!likely_if_innermost(x), likely_if_innermost(!x)) ||
        rewrite(!(!x && y), x || !y) ||
        rewrite(!(!x || y), x && !y) ||
        rewrite(!(x && !y), !x || y) ||
        rewrite(!(x || !y), !x && y) ||
        false) {
        return mutate(rewrite.result, info);
    }

    if (a.same_as(op->a)) {
        return op;
    } else {
        return Not::make(a);
    }
}

}  // namespace Internal
}  // namespace Halide
