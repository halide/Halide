#include "UnrollLoops.h"
#include "Bounds.h"
#include "CSE.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace Halide {
namespace Internal {

namespace {

// Build a per-iteration map of replacements for each Expr.
class FindReplacements : public IRVisitor {

    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (op->name == loop_var_name) {
            for (int i = 0; i < extent; i++) {
                if (min_is_zero) {
                    replacements[i][op] = i;
                } else {
                    replacements[i][op] = min + i;
                }
            }
        }
    }

    void visit(const Select *op) override {
        if (!min_is_zero) {
            IRVisitor::visit(op);
            return;
        }

        Expr rest;
        const Select *sel = op;
        std::set<int> handled;
        while (true) {
            auto result = solve_expression(sel->condition, loop_var_name);
            if (result.fully_solved) {
                const EQ *eq = result.result.as<EQ>();
                const int64_t *idx = eq ? as_const_int(eq->b) : nullptr;
                if (idx) {
                    int i = (int)(*idx);
                    Expr result = sel->true_value;
                    result = substitute(loop_var_name, i, result);
                    // Make a replacement for the original select true
                    // that maps directly to this leaf in this case.
                    // Note that we replace 'op', not sel. This is
                    // what saves all the work for deeply nested muxes
                    // and brings it down from O(n^2) to O(n)
                    replacements[i].emplace(op, result);
                    rest = sel->false_value;
                    sel = rest.as<Select>();
                    handled.insert(i);
                    if (!sel) {
                        // 'rest' was not a select node
                        break;
                    }
                } else {
                    // Condition was not of the form loop_var == int
                    break;
                }
            } else {
                // Failed to solve the condition for the loop var
                break;
            }
        }

        if (rest.defined()) {
            for (int i = 0; i < extent; i++) {
                if (!handled.count(i)) {
                    replacements[i].emplace(op, substitute(loop_var_name, i, rest));
                }
            }
            rest.accept(this);
        } else {
            IRVisitor::visit(op);
        }
    }

    string loop_var_name;
    Expr min;
    int extent = 0;
    bool min_is_zero = false;

public:
    // Maps from a loop iteration and an Expr and a loop iteration to
    // the correct replacement for that Expr for that iteration
    map<int, map<Expr, Expr, ExprCompare>> replacements;

    FindReplacements(string loop_var_name, Expr min, int extent)
        : loop_var_name(loop_var_name), min(min), extent(extent), min_is_zero(is_const_zero(min)) {
    }
};

class DoReplacements : public IRMutator {

    const map<Expr, Expr, ExprCompare> &replacements;

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        auto it = replacements.find(e);
        if (it != replacements.end()) {
            return it->second;
        } else {
            return IRMutator::mutate(e);
        }
    }

    DoReplacements(const map<Expr, Expr, ExprCompare> &replacements)
        : replacements(replacements) {
    }
};

class UnrollLoops : public IRMutator {
    using IRMutator::visit;

    vector<pair<string, Expr>> lets;

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

            FindReplacements replacer(for_loop->name, for_loop->min, e->value);
            body.accept(&replacer);

            Stmt iters;
            for (int i = e->value - 1; i >= 0; i--) {
                Stmt iter = DoReplacements(replacer.replacements[i]).mutate(body);
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
    return UnrollLoops().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
