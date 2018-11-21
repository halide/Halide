#include "UnsafePromises.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

class LowerUnsafePromises : public IRMutator2 {
    using IRMutator2::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::unsafe_promise_clamped)) {
            if (check) {
                Expr is_clamped = op->args[0] >= op->args[1] && op->args[0] <= op->args[2];
                std::ostringstream promise_expr_text;
                promise_expr_text << is_clamped;
                Expr cond_as_string = StringImm::make(promise_expr_text.str());
                Expr promise_broken_error =
                    Call::make(Int(32),
                               "halide_error_requirement_failed",
                               {cond_as_string, StringImm::make("from unsafe_promise_clamped")},
                               Call::Extern);
                return Call::make(op->args[0].type(),
                                  Call::require,
                                  {mutate(is_clamped), mutate(op->args[0]), promise_broken_error},
                                  Call::PureIntrinsic);
            } else {
                return mutate(op->args[0]);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }

    bool check;
public:
    LowerUnsafePromises(bool check) : check(check) {}
};

Stmt lower_unsafe_promises(Stmt s, const Target &t) {
    return LowerUnsafePromises(t.has_feature(Target::CheckUnsafePromises)).mutate(s);
}

}
}
