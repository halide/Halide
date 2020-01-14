#include "PurifyIndexMath.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

class PurifyIndexMath : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::signed_integer_overflow)) {
            // This should only occur for values that are evaluated
            // but never actually used (e.g. on select branches that
            // are unreachable). Just replace it with zero.
            return make_zero(op->type);
        } else {
            return IRMutator::visit(op);
        }
    }
};

Expr purify_index_math(Expr s) {
    return PurifyIndexMath().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
