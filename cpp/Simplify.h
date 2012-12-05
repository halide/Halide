#ifndef SIMPLIFY_H
#define SIMPLIFY_H

#include "IRMutator.h"
#include "Scope.h"

namespace HalideInternal {
    /* An IR mutator that performs a wide range of simplifications to
     * expressions and statements, including constant folding,
     * substituting in trivial values, arithmetic rearranging,
     * etc. Use it like so:
     * 
     * Stmt simpler_stmt = Simplify().mutate(stmt);
     * Expr simpler_expr = Simplify().mutate(expr);
     */

    class Simplify : public IRMutator {
        Scope<Expr> scope;

        void visit(const IntImm *);
        void visit(const FloatImm *);
        void visit(const Cast *);
        void visit(const Var *);
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

      public:
        static void test();
    };
};

#endif
