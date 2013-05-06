#include "Substitute.h"

namespace Halide { 
namespace Internal {

using std::string;

class Substitute : public IRMutator {
public:
    Substitute(string v, Expr r) : 
        var(v), replacement(r) {
    }

protected:
    string var;
    Expr replacement;

    using IRMutator::visit;

    void visit(const Variable *v) {
        if (v->name == var) expr = replacement;
        else expr = v;
    }   

    void visit(const Let *op) {
        if (op->name == var) {
            Expr new_value = mutate(op->value);
            if (new_value.same_as(op->value)) {
                expr = op;
            } else {
                expr = new Let(op->name, new_value, op->body);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (op->name == var) {
            Expr new_value = mutate(op->value);
            if (new_value.same_as(op->value)) {
                stmt = op;
            } else {
                stmt = new LetStmt(op->name, new_value, op->body);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const For *op) {
        if (op->name == var) {
            Expr new_min = mutate(op->min);
            Expr new_extent = mutate(op->extent);
            if (new_min.same_as(op->min) && new_extent.same_as(op->extent)) {
                stmt = op;
            } else {
                stmt = new For(op->name, new_min, new_extent, op->for_type, op->body);
            }
        } else {
            IRMutator::visit(op);
        }
    }

};

Expr substitute(string name, Expr replacement, Expr expr) {
    Substitute s(name, replacement);
    return s.mutate(expr);
}

Stmt substitute(string name, Expr replacement, Stmt stmt) {
    Substitute s(name, replacement);
    return s.mutate(stmt);
}

}
}
