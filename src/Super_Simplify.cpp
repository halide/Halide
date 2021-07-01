#include "Simplify.h"
#include "Simplify_Internal.h"

#include "CSE.h"
#include "CompilerLogger.h"
#include "IRMutator.h"
#include "IRMatch.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

class SuperSimplify : public IRMutator {
    using IRMutator::visit;

    // TODO(Evan): add visitor methods for all op types that have synthesized rewrite rules.

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type);
        
        if (op->type == Int(32)) {
            if (
              #include "Simplify_Add.inc"
              || false) {
                return mutate(rewrite.result);
            }
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Add::make(a, b);
        }
    }
};

Expr super_simplify(const Expr &expr) {
    return SuperSimplify().mutate(expr);
}

}  // namespace Internal
}  // namespace Halide
