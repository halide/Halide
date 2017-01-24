#include "UnifyDuplicateLets.h"
#include "IRMutator.h"
#include "IREquality.h"
#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::string;

class UnifyDuplicateLets : public IRMutator {
    using IRMutator::visit;

    map<Expr, string, IRDeepCompare> scope;
    map<string, string> rewrites;
    string producing;

public:
    using IRMutator::mutate;

    Expr mutate(Expr e) {

        if (e.defined()) {
            map<Expr, string, IRDeepCompare>::iterator iter = scope.find(e);
            if (iter != scope.end()) {
                expr = Variable::make(e.type(), iter->second);
            } else {
                e.accept(this);
            }
        } else {
            expr = Expr();
        }
        stmt = Stmt();
        return expr;
    }

protected:
    void visit(const Variable *op) {
        map<string, string>::iterator iter = rewrites.find(op->name);
        if (iter != rewrites.end()) {
            expr = Variable::make(op->type, iter->second);
        } else {
            expr = op;
        }
    }

    // Can't unify lets where the RHS might be not be pure
    bool is_impure;
    void visit(const Call *op) {
        is_impure |= !op->is_pure();
        IRMutator::visit(op);
    }

    void visit(const Load *op) {
        is_impure |= ((op->name == producing) ||
                      starts_with(op->name + ".", producing));
        IRMutator::visit(op);
    }

    void visit(const ProducerConsumer *op) {
        if (op->is_producer) {
            string old_producing = producing;
            producing = op->name;
            IRMutator::visit(op);
            producing = old_producing;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        is_impure = false;
        Expr value = mutate(op->value);
        Stmt body = op->body;

        bool should_pop = false;
        bool should_erase = false;

        if (!is_impure) {
            map<Expr, string, IRDeepCompare>::iterator iter = scope.find(value);
            if (iter == scope.end()) {
                scope[value] = op->name;
                should_pop = true;
            } else {
                value = Variable::make(value.type(), iter->second);
                rewrites[op->name] = iter->second;
                should_erase = true;
            }
        }

        body = mutate(op->body);

        if (should_pop) {
            scope.erase(value);
        }
        if (should_erase) {
            rewrites.erase(op->name);
        }

        if (value.same_as(op->value) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }
};

Stmt unify_duplicate_lets(Stmt s) {
    return UnifyDuplicateLets().mutate(s);
}

}
}
