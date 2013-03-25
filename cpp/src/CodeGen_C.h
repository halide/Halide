#ifndef HALIDE_CODEGEN_C_H
#define HALIDE_CODEGEN_C_H

/** \file
 * 
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt 
 */

#include "IRPrinter.h"
#include "Argument.h"
#include <string>
#include <vector>
#include <ostream>
#include <map>

namespace Halide { 
namespace Internal {

/** This class emits C++ code equivalent to a halide Stmt. It's
 * mostly the same as an IRPrinter, but it's wrapped in a function
 * definition, and some things are handled differently to be valid
 * C++.
 */
class CodeGen_C : public IRPrinter {
public:
    /** Initialize a C code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_C(std::ostream &);
 
    /** Emit source code equivalent to the given statement, wrapped in
     * a function with the given type signature */
    void compile(Stmt stmt, const std::string &name, const std::vector<Argument> &args);    

    /** Emit a header file defining a halide pipeline with the given
     * type signature */
    void compile_header(const std::string &name, const std::vector<Argument> &args);
    
    static void test();
    
protected:
    /** An for the most recently generated ssa variable */
    std::string id;

    /** A cache of generated values in scope */
    std::map<std::string, std::string> cache;

    /** Emit an expression as an assignment, then return the id of the
     * resulting var */
    std::string print_expr(Expr);
    
    /** Emit a statement */
    void print_stmt(Stmt);

    /** Emit the C name for a halide type */
    std::string print_type(Type);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    std::string print_name(const std::string &);    

    /** Emit an SSA-style assignment, and set id to the freshly generated name */
    void print_assignment(Type t, const std::string &rhs);

    /** Open a new C scope (i.e. throw in a brace, increase the indent) */
    void open_scope();

    /** Close a C scope (i.e. throw in an end brace, decrease the indent) */
    void close_scope();

    using IRPrinter::visit;

    void visit(const Variable *);
    void visit(const IntImm *);
    void visit(const FloatImm *);
    void visit(const Cast *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Max *);
    void visit(const Min *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Call *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Store *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const PrintStmt *);
    void visit(const AssertStmt *);
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Realize *);
    
    void visit_binop(Type t, Expr a, Expr b, const char *op);
};

}
}

#endif
