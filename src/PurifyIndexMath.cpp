#include "PurifyIndexMath.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class PurifyIndexMath : public IRMutator2 {
    using IRMutator2::visit;

    Expr visit(const Div *op) override {
        if (can_prove(op->b != 0)) {
            return op;
        } else {
            return Call::make(op->type, Call::quiet_div, {op->a, op->b}, Call::PureIntrinsic);
        }
    }

    Expr visit(const Mod *op) override {
        if (can_prove(op->b != 0)) {
            return op;
        } else {
            return Call::make(op->type, Call::quiet_mod, {op->a, op->b}, Call::PureIntrinsic);
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::indeterminate_expression) ||
            op->is_intrinsic(Call::signed_integer_overflow)) {
            // This will silently evaluate to an implementation-defined value.
            return undef(op->type);
        } else {
            return IRMutator2::visit(op);
        }
    }
};

Expr purify_index_math(Expr s) {
    return PurifyIndexMath().mutate(s);
}

}
}
