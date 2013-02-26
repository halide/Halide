#include "RemoveDeadLets.h"
#include "IRMutator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

class RemoveDeadLets : public IRMutator {
    Scope<int> references;

    using IRMutator::visit;

    void visit(const Variable *op) {
        if (references.contains(op->name)) references.ref(op->name)++;
        expr = op;
    }

    void visit(const For *op) {            
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        references.push(op->name, 0);
        Stmt body = mutate(op->body);
        references.pop(op->name);
        if (min.same_as(op->min) && extent.same_as(op->extent) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = new For(op->name, min, extent, op->for_type, body);
        }
    }

    void visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        references.push(op->name, 0);
        Stmt body = mutate(op->body);
        if (references.get(op->name) > 0) {
            if (body.same_as(op->body) && value.same_as(op->value)) {
                stmt = op;
            } else {
                stmt = new LetStmt(op->name, value, body);
            }
        } else {
            stmt = body;
        }
        references.pop(op->name);
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        references.push(op->name, 0);
        Expr body = mutate(op->body);
        if (references.get(op->name) > 0) {
            if (body.same_as(op->body) && value.same_as(op->value)) {
                expr = op;
            } else {
                expr = new Let(op->name, value, body);
            }
        } else {
            expr = body;
        }
        references.pop(op->name);
    }
};

Stmt remove_dead_lets(Stmt s) {
    return RemoveDeadLets().mutate(s);
}

}
}
