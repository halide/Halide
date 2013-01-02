#ifndef CODEGEN_X86_H
#define CODEGEN_X86_H

#include "CodeGen.h"

namespace Halide { 
namespace Internal {

/* A code generator that emits x86 code from a given Halide stmt. */

class CodeGen_X86 : public CodeGen {
public:

    CodeGen_X86(bool use_sse_41 = true);
        
    /* Compile to an llvm module. Takes a halide statement, the
     * name of the function produced, and the arguments to the
     * function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86
     * machine code. */
    void compile(Stmt stmt, string name, const vector<Argument> &args);

    static void test();

protected:
    bool use_sse_41;

    // Some useful types
    llvm::Type *i32x4, *i32x8;
    
    // Some variables used for matching
    Expr wild_i8x16, wild_i16x8, wild_i16x16, wild_i32x4, wild_i32x8;
    Expr wild_u8x16, wild_u16x8, wild_u32x4;
    Expr wild_f32x4, wild_f64x2;
    Expr min_i8, max_i8, min_i16, max_i16;
    Expr min_u8, max_u8u, max_u8i, min_u16, max_u16u, max_u16i;

    llvm::Value *call_intrin(Type t, const string &name, std::vector<Expr>);    

    // Nodes that we handle specially
    void visit(const Cast *);
    void visit(const Div *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const Allocate *);        
};

}}

#endif
