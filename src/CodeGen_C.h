#ifndef HALIDE_CODEGEN_C_H
#define HALIDE_CODEGEN_C_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt
 */

#include <string>
#include <vector>
#include <ostream>
#include <map>

#include "IRPrinter.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

struct Argument;

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
    CodeGen_C(std::ostream &dest, bool header = false, const std::string &name = "");
    ~CodeGen_C();

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

    static void test();

protected:
    /** An ID for the most recently generated ssa variable */
    std::string id;

    /** Controls whether this instance is generating declarations or
     * definitions. */
    bool header;

    /** A cache of generated values in scope */
    std::map<std::string, std::string> cache;

    /** Emit an expression as an assignment, then return the id of the
     * resulting var */
    std::string print_expr(Expr);

    /** Emit a statement */
    void print_stmt(Stmt);

    /** Emit the C name for a halide type */
    virtual std::string print_type(Type);

    /** Emit a statement to reinterpret an expression as another type */
    virtual std::string print_reinterpret(Type, Expr);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    virtual std::string print_name(const std::string &);

    /** Emit an SSA-style assignment, and set id to the freshly generated name. Return id. */
    std::string print_assignment(Type t, const std::string &rhs);

    /** Open a new C scope (i.e. throw in a brace, increase the indent) */
    void open_scope();

    /** Close a C scope (i.e. throw in an end brace, decrease the indent) */
    void close_scope(const std::string &comment);

    /** Unpack a buffer into its constituent parts and push it on the allocations stack. */
    void push_buffer(Type t, const std::string &buffer_name);

    /** Pop a buffer from the stack. */
    void pop_buffer(const std::string &buffer_name);

    /** Track the types of allocations to avoid unnecessary casts. */
    Scope<Type> allocations;

    /** Track which allocations actually went on the heap. */
    Scope<int> heap_allocations;

    /** True if there is a void * __user_context parameter in the arguments. */
    bool have_user_context;

    using IRPrinter::visit;

    void visit(const Variable *);
    void visit(const IntImm *);
    void visit(const StringImm *);
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
    void visit(const AssertStmt *);
    void visit(const Pipeline *);
    void visit(const For *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);
    void visit(const Return *);
    void visit(const FunctionDecl *);
    void visit(const BufferDecl *);

    void visit_binop(Type t, Expr a, Expr b, const char *op);
};

}
}

#endif
