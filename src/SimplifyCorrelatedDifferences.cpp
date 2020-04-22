#include "SimplifyCorrelatedDifferences.h"

#include "CSE.h"
#include "ExprUsesVar.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Monotonic.h"
#include "Scope.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {
namespace {

using std::pair;
using std::string;
using std::vector;

class SimplifyCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    string loop_var;

    Scope<Monotonic> monotonic;

    struct OuterLet {
        string name;
        Expr value;
        bool may_substitute;
    };
    vector<OuterLet> lets;

    template<typename LetStmtOrLet, typename StmtOrExpr>
    StmtOrExpr visit_let(const LetStmtOrLet *op) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const LetStmtOrLet *op;
            ScopedBinding<Monotonic> binding;
            Expr new_value;
            Frame(const LetStmtOrLet *op, const string &loop_var, Scope<Monotonic> &scope)
                : op(op),
                  binding(scope, op->name, is_monotonic(op->value, loop_var, scope)) {
            }
            Frame(const LetStmtOrLet *op)
                : op(op) {
            }
        };
        std::vector<Frame> frames;
        StmtOrExpr result;

        // Note that we must add *everything* that depends on the loop
        // var to the monotonic scope and the list of lets, even
        // things which we can never substitute in (e.g. impure
        // things). This is for two reasons. First this pass could be
        // used at a time when we still have nested lets under the
        // same name. If we decide not to add an inner let, but do add
        // the outer one, then later references to it will be
        // incorrect. Second, if we don't add something that happens
        // to be non-monotonic, then is_monotonic finds a variable
        // that references it in a later let, it will think it's a
        // constant, not an unknown.
        do {
            result = op->body;
            if (loop_var.empty()) {
                frames.emplace_back(op);
                continue;
            }

            bool pure = is_pure(op->value);
            if (!pure || expr_uses_vars(op->value, monotonic) || monotonic.contains(op->name)) {
                frames.emplace_back(op, loop_var, monotonic);
                Expr new_value = mutate(op->value);
                bool may_substitute_in = new_value.type() == Int(32) && pure;
                lets.emplace_back(OuterLet{op->name, new_value, may_substitute_in});
                frames.back().new_value = std::move(new_value);
            } else {
                // Pure and constant w.r.t the loop var. Doesn't
                // shadow any outer thing already in the monotonic
                // scope.
                frames.emplace_back(op);
            }
        } while ((op = result.template as<LetStmtOrLet>()));

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            if (it->new_value.defined()) {
                result = LetStmtOrLet::make(it->op->name, it->new_value, result);
            } else {
                result = LetStmtOrLet::make(it->op->name, it->op->value, result);
            }
            if (it->binding.bound()) {
                lets.pop_back();
            }
        }

        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let<Let, Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<LetStmt, Stmt>(op);
    }

    Stmt visit(const Store *op) override {
        // We only care about the expressions that determine the sizes
        // of allocations and loop extents, so no need to look inside
        // stores.
        return op;
    }

    Stmt visit(const Provide *op) override {
        return op;
    }

    Stmt visit(const For *op) override {
        Stmt s = op;
        // This is unfortunately quadratic in maximum loop nesting depth
        if (loop_var.empty()) {
            decltype(monotonic) tmp_monotonic;
            decltype(lets) tmp_lets;
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
            loop_var = op->name;
            {
                ScopedBinding<Monotonic> bind(monotonic, loop_var, Monotonic::Increasing);
                s = IRMutator::visit(op);
            }
            loop_var.clear();
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
        }
        s = IRMutator::visit(s.as<For>());
        return s;
    }

    class PartiallyCancelDifferences : public IRMutator {
        using IRMutator::visit;

        // Symbols used by rewrite rules
        IRMatcher::Wild<0> x;
        IRMatcher::Wild<1> y;
        IRMatcher::Wild<2> z;
        IRMatcher::WildConst<0> c0;
        IRMatcher::WildConst<1> c1;

        Expr visit(const Sub *op) override {

            Expr a = mutate(op->a), b = mutate(op->b);

            // Partially cancel terms in correlated differences of
            // various kinds to get tighter bounds.  We assume any
            // correlated term has already been pulled leftmost by
            // solve_expression.
            if (op->type == Int(32)) {
                auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type);
                if (
                    // Differences of quasi-affine functions
                    rewrite((x + y) / c0 - (x + z) / c0, ((x % c0) + y) / c0 - ((x % c0) + z) / c0, "cor165") ||
                    rewrite(x / c0 - (x + z) / c0, 0 - ((x % c0) + z) / c0, "cor166") ||
                    rewrite((x + y) / c0 - x / c0, ((x % c0) + y) / c0, "cor167") ||

                    // truncated cones have a constant upper or lower
                    // bound that isn't apparent when expressed in the
                    // form in the LHS below
                    rewrite(min(x, c0) - max(x, c1), min(min(c0 - x, x - c1), fold(min(0, c0 - c1))), "cor172") ||
                    rewrite(max(x, c0) - min(x, c1), max(max(c0 - x, x - c1), fold(max(0, c0 - c1))), "cor173") ||
                    rewrite(min(x, y) - max(x, z), min(min(x, y) - max(x, z), 0), "cor174") ||
                    rewrite(max(x, y) - min(x, z), max(max(x, y) - min(x, z), 0), "cor175") ||

                    false) {
                    return rewrite.result;
                }
            }
            return a - b;
        }
    };

    template<typename T>
    Expr visit_add_or_sub(const T *op) {
        if (op->type != Int(32) || loop_var.empty()) {
            return IRMutator::visit(op);
        }
        Expr e = IRMutator::visit(op);
        op = e.as<T>();
        if (!op) {
            return e;
        }
        auto ma = is_monotonic(op->a, loop_var, monotonic);
        auto mb = is_monotonic(op->b, loop_var, monotonic);

        if ((ma == Monotonic::Increasing && mb == Monotonic::Increasing && std::is_same<T, Sub>::value) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Decreasing && std::is_same<T, Sub>::value) ||
            (ma == Monotonic::Increasing && mb == Monotonic::Decreasing && std::is_same<T, Add>::value) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Increasing && std::is_same<T, Add>::value)) {

            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                if (expr_uses_var(e, it->name)) {
                    if (!it->may_substitute) {
                        // We have to stop here. Can't continue
                        // because there might be an outer let with
                        // the same name that we *can* substitute in,
                        // and then inner uses will get the wrong
                        // value.
                        break;
                    }
                }
                e = Let::make(it->name, it->value, e);
            }
            e = common_subexpression_elimination(e);
            e = solve_expression(e, loop_var).result;
            e = PartiallyCancelDifferences().mutate(e);
            e = simplify(e);

            if ((debug::debug_level() > 0) &&
                is_monotonic(e, loop_var) == Monotonic::Unknown) {
                // Might be a missed simplification opportunity. Log to help improve the simplifier.
                debug(1) << "Warning: expression is non-monotonic in loop variable "
                         << loop_var << ": " << e << "\n";
            }
        }
        return e;
    }

    Expr visit(const Sub *op) override {
        return visit_add_or_sub(op);
    }

    Expr visit(const Add *op) override {
        return visit_add_or_sub(op);
    }
};

}  // namespace

Stmt simplify_correlated_differences(const Stmt &stmt) {
    return SimplifyCorrelatedDifferences().mutate(stmt);
}

}  // namespace Internal
}  // namespace Halide
