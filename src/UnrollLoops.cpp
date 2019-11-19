#include "UnrollLoops.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

using std::vector;
using std::pair;

namespace Halide {
namespace Internal {

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

            class RemoveLikelyTags : public IRMutator {
                using IRMutator::visit;

                Expr visit(const Call *op) override {
                    if (op->is_intrinsic(Call::likely) ||
                        op->is_intrinsic(Call::likely_if_innermost)) {
                        internal_assert(op->args.size() == 1);
                        return mutate(op->args[0]);
                    } else {
                        return IRMutator::visit(op);
                    }
                }
            };

            Expr extent = simplify(RemoveLikelyTags().mutate(for_loop->extent));
            Stmt body = for_loop->body;
            const IntImm *e = extent.as<IntImm>();

            if (e == nullptr) {
                // We're about to hard fail. Get really aggressive
                // with the simplifier.
                for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                    extent = graph_substitute(it->first, it->second, extent);
                }
                extent = simplify(common_subexpression_elimination(extent));
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
                debug(1) << "Warning: not unrolling loop with extent " << extent << "\n";
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

Stmt unroll_loops(Stmt s) {
    return UnrollLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
