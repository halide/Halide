#include "SimplifyCorrelatedDifferences.h"

#include "CSE.h"
#include "ExprUsesVar.h"
#include "IROperator.h"
#include "IRMutator.h"
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
using std::pair;

class SimplifyCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    string loop_var;

    Scope<Monotonic> monotonic;

    vector<pair<string, Expr>> lets;

    template<typename LetStmtOrLet, typename StmtOrExpr>
    StmtOrExpr visit_let(const LetStmtOrLet *op) {
        if (op->value.type() != Int(32) ||
            !is_pure(op->value)) {
            // We only care about pure index. They must be pure because we're going to substitute them inwards.
            return IRMutator::visit(op);
        }
        auto m = is_monotonic(op->value, loop_var, monotonic);
        ScopedBinding<Monotonic> bind_monotonic(monotonic, op->name, m);
        lets.emplace_back(op->name, op->value);
        auto result = IRMutator::visit(op);
        lets.pop_back();
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
            s = IRMutator::visit(op);
            loop_var.clear();
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
        }
        s = IRMutator::visit(s.as<For>());
        return s;
    }

    template<typename T>
    Expr visit_add_or_sub(const T *op) {
        if (op->type != Int(32)) {
            return IRMutator::visit(op);
        }
        Expr e = IRMutator::visit(op);
        auto ma = is_monotonic(op->a, loop_var, monotonic);
        auto mb = is_monotonic(op->b, loop_var, monotonic);

        if ((ma == Monotonic::Increasing && mb == Monotonic::Increasing && std::is_same<T, Sub>::value) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Decreasing && std::is_same<T, Sub>::value) ||
            (ma == Monotonic::Increasing && mb == Monotonic::Decreasing && std::is_same<T, Add>::value) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Increasing && std::is_same<T, Add>::value)) {

            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                e = Let::make(it->first, it->second, e);
            }
            e = common_subexpression_elimination(e);
            e = solve_expression(e, loop_var).result;
            e = simplify(e);

            if ((debug::debug_level() > 0) &&
                is_monotonic(e, loop_var, monotonic) == Monotonic::Unknown) {
                // Might be a missed simplification opportunity. Log to help improve the simplifier.
                debug(1) << "Warning: expression is non-monotonic in loop variable " << loop_var << ": " << e << "\n";
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

Stmt simplify_correlated_differences(const Stmt &s) {
    return SimplifyCorrelatedDifferences().mutate(s);
}

}
}
