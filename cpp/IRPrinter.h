#ifndef IR_PRINTER_H
#define IR_PRINTER_H

#include "IRVisitor.h"
#include <ostream>

namespace HalideInternal {

    using std::ostream;

    // Serialize a chunk of IR into human-readable form
    class IRPrinter : public IRVisitor {
    private:
        ostream &stream;
        int indent;
    public:
        IRPrinter(Expr, ostream &);
        IRPrinter(Stmt, ostream &);

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

        static void test();
    };

    ostream &operator<<(ostream &stream, Expr);
    ostream &operator<<(ostream &stream, Stmt);
}

#endif
