#ifndef HALIDE_MODULUS_REMAINDER_H
#define HALIDE_MODULUS_REMAINDER_H

#include <utility>
#include "IRVisitor.h"
#include "Scope.h"

namespace Halide { 
namespace Internal {

/* For things like alignment analysis, often it's helpful to know
 * if an integer expression is some multiple of a constant plus
 * some other constant. For example, it is straight-forward to
 * deduce that ((10*x + 2)*(6*y - 3) - 1) is congruent to five
 * modulo six. 
 * 
 * We get the most information when the modulus is large. E.g. if
 * something is congruent to 208 modulo 384, then we also know
 * it's congruent to 0 mod 8, and we can possibly use it as an
 * index for an aligned load. If all else fails, we can just say
 * that an integer is congruent to zero modulo one. 
 */   
std::pair<int, int> modulus_remainder(Expr e);

/* Reduce an expression modulo some integer. Returns true if an
 * answer could be found. */
bool reduce_expr_modulo(Expr e, int modulus, int *remainder);

class ModulusRemainder : public IRVisitor {
public:
    std::pair<int, int> analyze(Expr e);

    static void test();
protected:
    int modulus, remainder;
    Scope<std::pair<int, int> > scope;

    void visit(const IntImm *);
    void visit(const FloatImm *);
    void visit(const Cast *);
    void visit(const Variable *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Call *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const PrintStmt *);
    void visit(const AssertStmt *);
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Realize *);
    void visit(const Block *);        
};

}
}

#endif
