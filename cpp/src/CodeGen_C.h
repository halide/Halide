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
    
    /** Emit the C name for a halide type */
    void print_c_type(Type);

    /** Emit a version of a stride that is a valid identifier in C (. is replaced with _) */
    void print_c_name(const std::string &);    

    using IRPrinter::visit;

    void visit(const Variable *);
    void visit(const Cast *);
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
    
};

}
}

#endif
