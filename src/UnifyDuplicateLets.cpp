#include "UnifyDuplicateLets.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Simplify.h"
#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::string;

namespace {

class UnifyDuplicateLets : public IRMutator {
    using IRMutator::visit;

    // Map from Exprs to a Variable in the let name that first introduced that
    // Expr.
    map<Expr, Expr, IRDeepCompare> scope;

    // Map from Vars to the Expr they should be replaced with.
    map<string, Expr> rewrites;

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        Expr new_e = IRMutator::mutate(e);

        if (auto iter = scope.find(new_e);
            iter != scope.end()) {
            return iter->second;
        }

        return new_e;
    }

protected:
    Expr visit(const Variable *op) override {
        auto iter = rewrites.find(op->name);
        if (iter != rewrites.end()) {
            return iter->second;
        } else {
            return op;
        }
    }

    // Can't unify lets where the RHS might be not be pure
    bool is_impure;
    Expr visit(const Call *op) override {
        is_impure |= !op->is_pure();
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        is_impure = true;
        return IRMutator::visit(op);
    }

    template<typename LetStmtOrLet>
    auto visit_let(const LetStmtOrLet *op) -> decltype(op->body) {
        is_impure = false;
        Expr value = mutate(op->value);
        Expr simplified = simplify(value);
        auto body = op->body;

        bool should_pop = false;
        bool should_erase = false;

        if (!is_impure) {
            if (simplified.as<Variable>() ||
                simplified.as<IntImm>()) {
                // The RHS collapsed to just a Var or a constant, so uses of
                // this should be rewritten to that value and we should drop
                // this let. The LetStmts at this point in lowering that we're
                // trying to remove are generally bounds inference expressions,
                // so it's not worth checking for other types of constant.
                rewrites[op->name] = simplified;
                should_erase = true;
            } else {
                Expr var = Variable::make(value.type(), op->name);

                // The mutate implementation above checks Exprs
                // post-mutation but without simplification, so we should
                // put the unsimplified version of the Expr into the scope.
                auto [it, inserted] = scope.emplace(value, std::move(var));

                if (inserted) {
                    should_pop = true;
                } else {
                    // We have the same RHS as some earlier Let
                    should_erase = true;
                    rewrites[op->name] = it->second;
                }
            }
        }

        body = mutate(op->body);

        if (should_pop) {
            scope.erase(value);
        }
        if (should_erase) {
            rewrites.erase(op->name);
            // We no longer need this let.
            return body;
        }

        if (simplified.same_as(op->value) && body.same_as(op->body)) {
            return op;
        } else {
            return LetStmtOrLet::make(op->name, simplified, body);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }
};

}  // namespace

Stmt unify_duplicate_lets(const Stmt &s) {
    return UnifyDuplicateLets().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
