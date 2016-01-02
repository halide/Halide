#include "Substitute.h"
#include "Scope.h"
#include "IRMutator.h"
#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;

class Substitute : public IRMutator {
    const map<string, Expr> &replace;
    Scope<int> hidden;

    Expr find_replacement(const string &s) {
        map<string, Expr>::const_iterator iter = replace.find(s);
        if (iter != replace.end() && !hidden.contains(s)) {
            return iter->second;
        } else {
            return Expr();
        }
    }

public:
    Substitute(const map<string, Expr> &m) : replace(m) {}

    using IRMutator::visit;

    void visit(const Variable *v) {
        Expr r = find_replacement(v->name);
        if (r.defined()) {
            expr = r;
        } else {
            expr = v;
        }
    }

    void visit(const Let *op) {
        Expr new_value = mutate(op->value);
        hidden.push(op->name, 0);
        Expr new_body = mutate(op->body);
        hidden.pop(op->name);

        if (new_value.same_as(op->value) &&
            new_body.same_as(op->body)) {
            expr = op;
        } else {
            expr = Let::make(op->name, new_value, new_body);
        }
    }

    void visit(const LetStmt *op) {
        Expr new_value = mutate(op->value);
        hidden.push(op->name, 0);
        Stmt new_body = mutate(op->body);
        hidden.pop(op->name);

        if (new_value.same_as(op->value) &&
            new_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, new_value, new_body);
        }
    }

    void visit(const For *op) {

        Expr new_min = mutate(op->min);
        Expr new_extent = mutate(op->extent);
        hidden.push(op->name, 0);
        Stmt new_body = mutate(op->body);
        hidden.pop(op->name);

        if (new_min.same_as(op->min) &&
            new_extent.same_as(op->extent) &&
            new_body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, new_min, new_extent, op->for_type, op->device_api, new_body);
        }
    }

};

Expr substitute(string name, Expr replacement, Expr expr) {
    map<string, Expr> m;
    m[name] = replacement;
    Substitute s(m);
    return s.mutate(expr);
}

Stmt substitute(string name, Expr replacement, Stmt stmt) {
    map<string, Expr> m;
    m[name] = replacement;
    Substitute s(m);
    return s.mutate(stmt);
}

Expr substitute(const map<string, Expr> &m, Expr expr) {
    Substitute s(m);
    return s.mutate(expr);
}

Stmt substitute(const map<string, Expr> &m, Stmt stmt) {
    Substitute s(m);
    return s.mutate(stmt);
}


class SubstituteExpr : public IRMutator {
public:
    Expr find, replacement;

    using IRMutator::mutate;

    Expr mutate(Expr e) {
        if (equal(e, find)) {
            return replacement;
        } else {
            return IRMutator::mutate(e);
        }
    }
};

Expr substitute(Expr find, Expr replacement, Expr expr) {
    SubstituteExpr s;
    s.find = find;
    s.replacement = replacement;
    return s.mutate(expr);
}

Stmt substitute(Expr find, Expr replacement, Stmt stmt) {
    SubstituteExpr s;
    s.find = find;
    s.replacement = replacement;
    return s.mutate(stmt);
}

}
}
