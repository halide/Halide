#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Not *op, ExprInfo *bounds) {
    Expr a = mutate(op->a, nullptr);

    auto rewrite = IRMatcher::rewriter(IRMatcher::not_op(a), op->type);

    if (rewrite(!c0, fold(!c0), "not11") ||
        rewrite(!(x < y), y <= x, "not12") ||
        rewrite(!(x <= y), y < x, "not13") ||
        rewrite(!(x == y), x != y, "not16") ||
        rewrite(!(x != y), x == y, "not17") ||
        rewrite(!!x, x, "not18")) {
        return rewrite.result;
    }

    if ((rewrite(!broadcast(x), broadcast(!x, op->type.lanes()), "not22")) ||
        (rewrite(!intrin(Call::likely, x), intrin(Call::likely, !x), "not23")) ||
        (rewrite(!intrin(Call::likely_if_innermost, x), intrin(Call::likely_if_innermost, !x), "not24"))) {
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