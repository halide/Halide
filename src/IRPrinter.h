#ifndef HALIDE_IR_PRINTER_H
#define HALIDE_IR_PRINTER_H

/** \file
 * This header file defines operators that let you dump a Halide
 * expression, statement, or type directly into an output stream
 * in a human readable form.
 * E.g:
 \code
 Expr foo = ...
 std::cout << "Foo is " << foo << std::endl;
 \endcode
 *
 * These operators are implemented using \ref Halide::Internal::IRPrinter
 */

#include <ostream>

#include "Module.h"
#include "IRVisitor.h"

namespace Halide {

/** Emit an expression on an output stream (such as std::cout) in a
 * human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const Expr &);

/** Emit a halide type on an output stream (such as std::cout) in a
 * human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const Type &);

/** Emit a halide Module on an output stream (such as std::cout) in a
 * human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const Module &);

/** Emit a halide device api type in a human readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const DeviceAPI &);

namespace Internal {

struct AssociativePattern;
struct AssociativeOp;

/** Emit a halide associative pattern on an output stream (such as std::cout)
 * in a human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const AssociativePattern &);

/** Emit a halide associative op on an output stream (such as std::cout) in a
 * human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const AssociativeOp &);

/** Emit a halide statement on an output stream (such as std::cout) in
 * a human-readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const Stmt &);

/** Emit a halide for loop type (vectorized, serial, etc) in a human
 * readable form */
EXPORT std::ostream &operator<<(std::ostream &stream, const ForType &);

/** Emit a halide name mangling value in a human readable format */
EXPORT std::ostream &operator<<(std::ostream &stream, const NameMangling &);

/** An IRVisitor that emits IR to the given output stream in a human
 * readable form. Can be subclassed if you want to modify the way in
 * which it prints.
 */
class IRPrinter : public IRVisitor {
public:
    /** Construct an IRPrinter pointed at a given output stream
     * (e.g. std::cout, or a std::ofstream) */
    EXPORT IRPrinter(std::ostream &);

    /** emit an expression on the output stream */
    EXPORT void print(Expr);

    /** emit a statement on the output stream */
    EXPORT void print(Stmt);

    /** emit a comma delimited list of exprs, without any leading or
     * trailing punctuation. */
    EXPORT void print_list(const std::vector<Expr> &exprs);

    EXPORT static void test();

protected:
    /** The stream we're outputting on */
    std::ostream &stream;

    /** The current indentation level, useful for pretty-printing
     * statements */
    int indent;

    /** Emit spaces according to the current indentation level */
    void do_indent();

    void visit(const IntImm *);
    void visit(const UIntImm *);
    void visit(const FloatImm *);
    void visit(const StringImm *);
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
    void visit(const AssertStmt *);
    void visit(const ProducerConsumer *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const Block *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);
    void visit(const Shuffle *);
    void visit(const Prefetch *);
};
}
}

#endif
