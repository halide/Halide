#include "SimplifyCorrelatedDifferences.h"

#include "CSE.h"
#include "CompilerLogger.h"
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

using std::string;
using std::vector;

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
                rewrite((x + y) / c0 - (x + z) / c0, ((x % c0) + y) / c0 - ((x % c0) + z) / c0) ||
                rewrite(x / c0 - (x + z) / c0, 0 - ((x % c0) + z) / c0) ||
                rewrite((x + y) / c0 - x / c0, ((x % c0) + y) / c0) ||

                // truncated cones have a constant upper or lower
                // bound that isn't apparent when expressed in the
                // form in the LHS below
                rewrite(min(x, c0) - max(x, c1), min(min(c0 - x, x - c1), fold(min(0, c0 - c1)))) ||
                rewrite(max(x, c0) - min(x, c1), max(max(c0 - x, x - c1), fold(max(0, c0 - c1)))) ||
                rewrite(min(x, y) - max(x, z), min(min(x, y) - max(x, z), 0)) ||
                rewrite(max(x, y) - min(x, z), max(max(x, y) - min(x, z), 0)) ||

                rewrite(min(x + c0, y) - select(z, min(x, y) + c1, x), select(z, (max(min(y - x, c0), 0) - c1), min(y - x, c0)), c0 > 0) ||
                rewrite(min(y, x + c0) - select(z, min(y, x) + c1, x), select(z, (max(min(y - x, c0), 0) - c1), min(y - x, c0)), c0 > 0) ||

                false) {
                return rewrite.result;
            }
        }
        return a - b;
    }
};

class SimplifyCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    string loop_var;

    Scope<ConstantInterval> monotonic;

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
            ScopedBinding<ConstantInterval> binding;
            Expr new_value;
            Frame(const LetStmtOrLet *op, const string &loop_var, Scope<ConstantInterval> &scope)
                : op(op),
                  binding(scope, op->name, derivative_bounds(op->value, loop_var, scope)) {
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
        // to be non-monotonic, then derivative_bounds finds a variable
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
                ScopedBinding<ConstantInterval> bind(monotonic, loop_var, ConstantInterval::single_point(1));
                s = IRMutator::visit(op);
            }
            loop_var.clear();
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
        }
        s = IRMutator::visit(s.as<For>());
        return s;
    }

    // Add the names of any free variables in an expr to the provided set
    void track_free_vars(const Expr &e, std::set<std::string> *vars) {
        class TrackFreeVars : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Variable *op) override {
                if (!scope.contains(op->name)) {
                    vars->insert(op->name);
                }
            }
            void visit(const Let *op) override {
                ScopedBinding<> bind(scope, op->name);
                IRVisitor::visit(op);
            }

        public:
            std::set<std::string> *vars;
            Scope<> scope;
            TrackFreeVars(std::set<std::string> *vars)
                : vars(vars) {
            }
        } tracker(vars);
        e.accept(&tracker);
    }

    Expr cancel_correlated_subexpression(Expr e, const Expr &a, const Expr &b, bool correlated) {
        auto ma = is_monotonic(a, loop_var, monotonic);
        auto mb = is_monotonic(b, loop_var, monotonic);

        if ((ma == Monotonic::Increasing && mb == Monotonic::Increasing && correlated) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Decreasing && correlated) ||
            (ma == Monotonic::Increasing && mb == Monotonic::Decreasing && !correlated) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Increasing && !correlated)) {

            std::set<std::string> vars;
            track_free_vars(e, &vars);

            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                if (!it->may_substitute && vars.count(it->name)) {
                    // We have to stop here. Can't continue
                    // because there might be an outer let with
                    // the same name that we *can* substitute in,
                    // and then inner uses will get the wrong
                    // value.
                    break;
                }
                track_free_vars(it->value, &vars);
                e = Let::make(it->name, it->value, e);
            }
            e = common_subexpression_elimination(e);
            e = solve_expression(e, loop_var).result;
            e = PartiallyCancelDifferences().mutate(e);
            e = simplify(e);

            const bool check_non_monotonic = debug::debug_level() > 0 || get_compiler_logger() != nullptr;
            if (check_non_monotonic &&
                is_monotonic(e, loop_var) == Monotonic::Unknown) {
                // Might be a missed simplification opportunity. Log to help improve the simplifier.
                if (get_compiler_logger()) {
                    get_compiler_logger()->record_non_monotonic_loop_var(loop_var, e);
                }
                debug(1) << "Warning: expression is non-monotonic in loop variable "
                         << loop_var << ": " << e << "\n";
            }
        }
        return e;
    }

    template<typename T>
    Expr visit_binop(const T *op, bool correlated) {
        Expr e = IRMutator::visit(op);
        op = e.as<T>();
        if (op == nullptr ||
            op->a.type() != Int(32) ||
            loop_var.empty()) {
            return e;
        } else {
            // Bury the logic that doesn't depend on the template
            // parameter in a separate function to save code size and
            // reduce stack frame size in the recursive path.
            return cancel_correlated_subexpression(e, op->a, op->b, correlated);
        }
    }

    // Binary ops where it pays to cancel a correlated term on both
    // sides. E.g. consider the x in:
    //
    // (x*3 + y)*2 - max(x*6, 0)))
    //
    // Both sides increase monotonically with x so interval arithmetic
    // will overestimate the bounds. If we subtract x*6 from both
    // sides we get:
    //
    // y*2 - max(0, x*-6)
    //
    // Now only one side depends on x and interval arithmetic becomes
    // exact.
    Expr visit(const Sub *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const LT *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const LE *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const GT *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const GE *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const EQ *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const NE *op) override {
        return visit_binop(op, true);
    }

    // For add you actually want to cancel any anti-correlated term
    // (e.g. x in (x*3 + y)*2 + max(x*-6, 0))
    Expr visit(const Add *op) override {
        return visit_binop(op, false);
    }
};

}  // namespace

Stmt simplify_correlated_differences(const Stmt &stmt) {
    return SimplifyCorrelatedDifferences().mutate(stmt);
}

Expr bound_correlated_differences(const Expr &expr) {
    return PartiallyCancelDifferences().mutate(expr);
}

}  // namespace Internal
}  // namespace Halide
