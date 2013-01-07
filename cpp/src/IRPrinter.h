#ifndef HALIDE_IR_PRINTER_H
#define HALIDE_IR_PRINTER_H

#include "IRVisitor.h"
#include "Type.h"
#include <ostream>

namespace Halide { 

/* 
   This header file defines operators that let you dump a Halide
   expression, statement, or type directly into an output stream
   in a human readable form.
   E.g:

   Expr foo = ...
   std::cout << "Foo is " << foo << std::endl;
*/

std::ostream &operator<<(std::ostream &stream, Expr);
std::ostream &operator<<(std::ostream &stream, Stmt);
std::ostream &operator<<(std::ostream &stream, Type);

/* 
   These operators are implemented using the IRPrinter class
   below. You probably don't need to use it directly, unless
   you're subclassing it. Construct it with an output stream
   (e.g. std::cout, or an ostringstream), and call the print
   method on an expression or statement.
*/

namespace Internal {

class IRPrinter : public IRVisitor {
public:
    IRPrinter(std::ostream &);
    void print(Expr);
    void print(Stmt);

    static void test();

protected:
    std::ostream &stream;
    int indent;

    void do_indent();

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
