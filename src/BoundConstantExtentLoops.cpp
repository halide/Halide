#include "BoundConstantExtentLoops.h"
#include "BoundsTracker.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {
class BoundLoops : public IRMutator {
protected:
    using IRMutator::visit;

    BoundsTracker tracker;

    Stmt visit(const LetStmt *op) override {
        auto binding = tracker.push_let(op->name, op->value);
        return IRMutator::visit(op);
    }

    Stmt visit(const IfThenElse *op) override {
        Stmt then_case, else_case;
        {
            auto fact = tracker.push_fact(op->condition);
            then_case = mutate(op->then_case);
        }
        if (op->else_case.defined()) {
            auto fact = tracker.push_fact(simplify(!op->condition));
            else_case = mutate(op->else_case);
        }
        if (then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

    Stmt visit(const For *op) override {
        auto bind = tracker.push_for(op->name, op->min, op->max);
        Expr extent = simplify(op->extent());
        if (is_const(extent)) {
            // Nothing needs to be done
            return IRMutator::visit(op);
        }

        if (op->for_type == ForType::Unrolled ||
            op->for_type == ForType::Vectorized) {
            // Give it one last chance to simplify to an int
            extent = tracker.simplify_with_context(extent);
            Stmt body = op->body;
            std::optional<int64_t> e = as_const_int(extent);

            if (!e) {
                // We're about to hard fail. Get really aggressive with the
                // simplifier: inline every enclosing let and simplify under
                // every dominating condition.
                debug(4) << "Trying to find a constant bound for loop " << op->name << "\n"
                         << "Extent: " << extent << "\n";
                ConstantInterval bounds = tracker.find_constant_bounds(extent);
                debug(4) << "Bounds found: " << bounds << "\n";
                if (bounds.is_single_point()) {
                    // The bound is exact: no guard needed.
                    e = bounds.min;
                } else if (bounds.max_defined) {
                    // Still no luck getting an exact extent. Take the upper
                    // bound instead and guard the body with an if statement.
                    debug(4) << "Found an upper bound instead: " << bounds.max << "\n";
                    e = bounds.max;
                    body =
                        IfThenElse::make(likely_if_innermost(Variable::make(Int(32), op->name) <=
                                                             op->max),
                                         body);
                }
            }

            if (!e && permit_failed_unroll && op->for_type == ForType::Unrolled) {
                // Still no luck, but we're allowed to fail. Rewrite
                // to a serial loop.
                user_warning << "HL_PERMIT_FAILED_UNROLL is allowing us to unroll a non-constant loop into a serial loop. Did you mean to do this?\n";
                body = mutate(body);
                return For::make(op->name, op->min, op->max,
                                 ForType::Serial, op->partition_policy, op->device_api, std::move(body));
            }

            user_assert(e)
                << "Can only " << (op->for_type == ForType::Unrolled ? "unroll" : "vectorize")
                << " for loops over a constant extent.\n"
                << "Loop over " << op->name << " has extent " << extent << ".\n";
            body = mutate(body);

            return op->with(op->min, (op->min + make_const(extent.type(), *e)) - 1, body);
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
    return BoundLoops()(s);
}

}  // namespace Internal
}  // namespace Halide
