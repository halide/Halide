#include "UnrollLoops.h"
#include "Bounds.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"

using std::pair;
using std::vector;

namespace Halide {
namespace Internal {

namespace {

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    vector<pair<std::string, Expr>> lets;

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

    Stmt visit(const For *for_loop) override {
        if (for_loop->for_type == ForType::Unrolled) {
            // Give it one last chance to simplify to an int
            Expr extent = simplify(for_loop->extent);
            Stmt body = for_loop->body;
            const IntImm *e = extent.as<IntImm>();

            if (e == nullptr) {
                // We're about to hard fail. Get really aggressive
                // with the simplifier.
                for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                    extent = Let::make(it->first, it->second, extent);
                }
                extent = remove_likelies(extent);
                extent = substitute_in_all_lets(extent);
                extent = simplify(extent);
                e = extent.as<IntImm>();
            }

            Expr extent_upper;
            bool use_guard = false;
            if (e == nullptr) {
                // Still no luck. Try taking an upper bound and
                // injecting an if statement around the body.
                extent_upper = find_constant_bound(extent, Direction::Upper, Scope<Interval>());
                if (extent_upper.defined()) {
                    e = extent_upper.as<IntImm>();
                    use_guard = true;
                }
            }

            if (e == nullptr && permit_failed_unroll) {
                // Still no luck, but we're allowed to fail. Rewrite
                // to a serial loop.
                user_warning << "HL_PERMIT_FAILED_UNROLL is allowing us to unroll a non-constant loop into a serial loop. Did you mean to do this?\n";
                body = mutate(body);
                return For::make(for_loop->name, for_loop->min, for_loop->extent,
                                 ForType::Serial, for_loop->device_api, std::move(body));
            }

            user_assert(e)
                << "Can only unroll for loops over a constant extent.\n"
                << "Loop over " << for_loop->name << " has extent " << extent << ".\n";
            body = mutate(body);

            if (e->value == 1) {
                user_warning << "Warning: Unrolling a for loop of extent 1: " << for_loop->name << "\n";
            }

            Stmt iters;
            for (int i = e->value - 1; i >= 0; i--) {
                Stmt iter = substitute(for_loop->name, for_loop->min + i, body);
                // It's necessary to eagerly simplify this iteration
                // here to resolve things like muxes down to a single
                // item before we go and make N copies of something of
                // size N.
                iter = simplify(iter);
                if (!iters.defined()) {
                    iters = iter;
                } else {
                    iters = Block::make(iter, iters);
                }
                if (use_guard) {
                    iters = IfThenElse::make(likely_if_innermost(i < for_loop->extent), iters);
                }
            }

            return iters;

        } else {
            return IRMutator::visit(for_loop);
        }
    }
    bool permit_failed_unroll = false;

public:
    UnrollLoops() {
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

Stmt unroll_loops(const Stmt &s) {
    Stmt stmt = UnrollLoops().mutate(s);
    // Unrolling duplicates variable names. Other passes assume variable names are unique.
    return uniquify_variable_names(stmt);
}

}  // namespace Internal
}  // namespace Halide
