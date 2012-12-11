#include <assert.h>
#include "ModulusRemainder.h"
#include "IR.h"

// This file is largely a port of parts of src/analysis.ml

namespace Halide { 
namespace Internal {

using std::make_pair;

pair<int, int> modulus_remainder(Expr e) {
    ModulusRemainder mr;
    return mr.analyze(e);        
}

bool reduce_expr_modulo(Expr expr, int modulus, int *remainder) {
    pair<int, int> result = modulus_remainder(expr);

    /* As an example: If we asked for expr mod 8, and the analysis
     * said that expr = 16*k + 13, then because 16 % 8 == 0, the
     * result is 13 % 8 == 5. But if the analysis says that expr =
     * 6*k + 3, then expr mod 8 could be 1, 3, 5, or 7, so we just
     * return false. 
     */

    if (result.first % modulus == 0) {
        *remainder = result.second % modulus;
        return true;
    } else {
        return false;            
    }
}    

pair<int, int> ModulusRemainder::analyze(Expr e) {
    e.accept(this);
    return make_pair(modulus, remainder);
}

void ModulusRemainder::test() {
    // TODO;
}


void ModulusRemainder::visit(const IntImm *op) {
    // Equal to op->value modulo anything. We'll use zero as the
    // modulus to mark this special case. We'd better be able to
    // handle zero in the rest of the code...
    remainder = op->value;
    modulus = 0;
}

void ModulusRemainder::visit(const FloatImm *) {
    assert(false && "modulus_remainder of float");
}

void ModulusRemainder::visit(const Cast *) {
    modulus = 1;
    remainder = 0;
}

void ModulusRemainder::visit(const Variable *op) {
    if (scope.contains(op->name)) {
        pair<int, int> mod_rem = scope.get(op->name);
        modulus = mod_rem.first;
        remainder = mod_rem.second;
    } else {
        modulus = 1;
        remainder = 0;
    }
}

int gcd(int a, int b) {
    if (a < b) std::swap(a, b);
    while (b != 0) {
        int tmp = b;
        b = a % b;
        a = tmp;            
    }
    return a;
}

int mod(int a, int m) {
    if (m == 0) return a;
    a = a % m;
    if (a < 0) a += m;
    return a;
}

void ModulusRemainder::visit(const Add *op) {
    pair<int, int> a = analyze(op->a);
    pair<int, int> b = analyze(op->b);
    modulus = gcd(a.first, b.first);
    remainder = mod(a.second + b.second, modulus);
}

void ModulusRemainder::visit(const Sub *) {
    // TODO
}

void ModulusRemainder::visit(const Mul *) {
    // TODO
}

void ModulusRemainder::visit(const Div *) {
    // TODO
}

void ModulusRemainder::visit(const Mod *) {
    // TODO
} 

void ModulusRemainder::visit(const Min *) {
    // TODO
}

void ModulusRemainder::visit(const Max *) {
    // TODO
}

void ModulusRemainder::visit(const EQ *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const NE *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const LT *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const LE *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const GT *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const GE *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const And *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const Or *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const Not *) {
    assert(false && "modulus_remainder of bool");
}

void ModulusRemainder::visit(const Select *) {
    // TODO
}

void ModulusRemainder::visit(const Load *) {
    modulus = 1;
    remainder = 0;
}

void ModulusRemainder::visit(const Ramp *) {
    assert(false && "modulus_remainder of vector");
}

void ModulusRemainder::visit(const Broadcast *) {
    assert(false && "modulus_remainder of vector");
}

void ModulusRemainder::visit(const Call *) {
    modulus = 1;
    remainder = 0;
}

void ModulusRemainder::visit(const Let *op) {
    pair<int, int> val = analyze(op->value);
    scope.push(op->name, val);
    val = analyze(op->body);
    scope.pop(op->name);

    modulus = val.first;
    remainder = val.second;
}

void ModulusRemainder::visit(const LetStmt *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const PrintStmt *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const AssertStmt *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Pipeline *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const For *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Store *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Provide *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Allocate *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Realize *) {
    assert(false && "modulus_remainder of statement");
}

void ModulusRemainder::visit(const Block *) {
    assert(false && "modulus_remainder of statement");
}

}
}
