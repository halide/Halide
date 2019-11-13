#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Not *op, ExprInfo *bounds) {
    Expr a = mutate(op->a, nullptr);

    auto rewrite = IRMatcher::rewriter(IRMatcher::not_op(a), op->type);

    if (rewrite(!c0, fold(!c0)) ||
        rewrite(!(x < y), y <= x) ||
        rewrite(!(x <= y), y < x) ||
        rewrite(!(x > y), y >= x) ||
        rewrite(!(x >= y), y > x) ||
        rewrite(!(x == y), x != y) ||
        rewrite(!(x != y), x == y) ||
        rewrite(!!x, x)) {
        return rewrite.result;
    }

    if (rewrite(!broadcast(x), broadcast(!x, op->type.lanes())) ||
        rewrite(!intrin(Call::likely, x), intrin(Call::likely, !x)) ||
        rewrite(!intrin(Call::likely_if_innermost, x), intrin(Call::likely_if_innermost, !x))) {
        return mutate(std::move(rewrite.result), bounds);
    }

    if (a.same_as(op->a)) {
        return op;
    } else {
        return Not::make(a);
    }
}

}  // namespace Internal
}  // namespace Halide
