#include "Substitute.h"

namespace HalideInternal {

    Expr substitute(string name, Expr replacement, Expr expr) {
        Substitute s(name, replacement);
        return s.mutate(expr);
    }

    Stmt substitute(string name, Expr replacement, Stmt stmt) {
        Substitute s(name, replacement);
        return s.mutate(stmt);
    }
    
    Substitute::Substitute(string v, Expr r) : 
        var(v), replacement(r) {
    }

    void Substitute::visit(const Var *v) {
        if (v->name == var) expr = replacement;
        else expr = v;
    };



}
