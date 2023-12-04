#include "BoundConstantExtentLoops.h"
#include "Bounds.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {
class BoundLoops : public IRMutator {
    using IRMutator::visit;

    std::vector<std::pair<std::string, Expr>> lets;

    Stmt visit(const LetStmt *op) override {
        if (is_pure(op->value)) {
            lets.emplace_back(op->name, op->value);
            Stmt s = IRMutator::visit(op);
            lets.pop_back();
            return s;
        } else {
            return IRMutator::visit(op);
        }
    }

    std::vector<Expr> facts;
    Stmt visit(const IfThenElse *op) override {
        facts.push_back(op->condition);
        Stmt then_case = mutate(op->then_case);
        Stmt else_case;
        if (op->else_case.defined()) {
            facts.back() = simplify(!op->condition);
            else_case = mutate(op->else_case);
        }
        facts.pop_back();
        if (then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

    Stmt visit(const For *op) override {
        if (is_const(op->extent)) {
            // Nothing needs to be done
            return IRMutator::visit(op);
        }

        if (op->for_type == ForType::Unrolled ||
            op->for_type == ForType::Vectorized) {
            // Give it one last chance to simplify to an int
            Expr extent = simplify(op->extent);
            Stmt body = op->body;
            const IntImm *e = extent.as<IntImm>();

            if (e == nullptr) {
                // We're about to hard fail. Get really aggressive
                // with the simplifier.
                for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                    extent = Let::make(it->first, it->second, extent);
                }
                extent = remove_likelies(extent);
                extent = substitute_in_all_lets(extent);
                extent = simplify(extent,
                                  true,
                                  Scope<Interval>::empty_scope(),
                                  Scope<ModulusRemainder>::empty_scope(),
                                  facts);
                e = extent.as<IntImm>();
            }

            Expr extent_upper;
            if (e == nullptr) {
                // Still no luck. Try taking an upper bound and
                // injecting an if statement around the body.
                extent_upper = find_constant_bound(extent, Direction::Upper, Scope<Interval>());
                if (extent_upper.defined()) {
                    e = extent_upper.as<IntImm>();
                    body =
                        IfThenElse::make(likely_if_innermost(Variable::make(Int(32), op->name) <
                                                             op->min + op->extent),
                                         body);
                }
            }

            if (e == nullptr && permit_failed_unroll && op->for_type == ForType::Unrolled) {
                // Still no luck, but we're allowed to fail. Rewrite
                // to a serial loop.
                user_warning << "HL_PERMIT_FAILED_UNROLL is allowing us to unroll a non-constant loop into a serial loop. Did you mean to do this?\n";
                body = mutate(body);
                return For::make(op->name, op->min, op->extent,
                                 ForType::Serial, op->partition_policy, op->device_api, std::move(body));
            }

            user_assert(e)
                << "Can only " << (op->for_type == ForType::Unrolled ? "unroll" : "vectorize")
                << " for loops over a constant extent.\n"
                << "Loop over " << op->name << " has extent " << extent << ".\n";
            body = mutate(body);

            return For::make(op->name, op->min, e,
                             op->for_type, op->partition_policy, op->device_api, std::move(body));
        } else {
            return IRMutator::visit(op);
        }
    }
    bool permit_failed_unroll = false;

public:
    BoundLoops() {
        // Experimental autoschedulers may want to unroll without
        // being totally confident the loop will indeed turn out
        // to be constant-sized. If this feature continues to be
        // important, we need to expose it in the scheduling
        // language somewhere, but how? For now we do something
        // ugly and expedient.

        // For the tracking issue to fix this, see
        // https://github.com/halide/Halide/issues/3479
        permit_failed_unroll = get_env_variable("HL_PERMIT_FAILED_UNROLL") == "1";
    }
};

}  // namespace

Stmt bound_constant_extent_loops(const Stmt &s) {
    return BoundLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
