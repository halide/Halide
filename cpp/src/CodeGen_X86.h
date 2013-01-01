#ifndef CODEGEN_X86_H
#define CODEGEN_X86_H

#include "CodeGen.h"

namespace Halide { 
namespace Internal {

/* A code generator that emits x86 code from a given Halide stmt. */

class CodeGen_X86 : public CodeGen {
public:

    CodeGen_X86();
        
    /* Compile to an llvm module. Takes a halide statement, the
     * name of the function produced, and the arguments to the
     * function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86
     * machine code. */
    void compile(Stmt stmt, string name, const vector<Argument> &args);

    static void test();

protected:
    // Some useful types
    llvm::Type *i32x4, *i32x8;

    llvm::Value *call_intrin(Type t, const string &name, Expr arg1, Expr arg2 = Expr());    

    // Nodes that we handle specially
    void visit(const Cast *);
    void visit(const Allocate *);        


};

}}

#endif
