#ifndef HALIDE_CODEGEN_X86_H
#define HALIDE_CODEGEN_X86_H

#include "CodeGen.h"

namespace Halide { 
namespace Internal {

/* A code generator that emits x86 code from a given Halide stmt. */

class CodeGen_X86 : public CodeGen {
public:

    CodeGen_X86(bool use_sse_41 = true, bool use_avx = true);
        
    /* Compile to an llvm module. Takes a halide statement, the
     * name of the function produced, and the arguments to the
     * function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86
     * machine code. */
    void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);

    static void test();

protected:
    bool use_sse_41, use_avx;

    // Some useful types
    llvm::Type *i32x4, *i32x8;
    
    // Some variables used for matching
    Expr wild_i8x16, wild_i16x8, wild_i16x16, wild_i32x4, wild_i32x8;
    Expr wild_u8x16, wild_u16x8, wild_u32x4;
    Expr wild_f32x4, wild_f32x8, wild_f64x2;

    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>);    
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<llvm::Value *>);    

    // Nodes that we handle specially
    void visit(const Cast *);
    void visit(const Div *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const Allocate *);        

    std::stack<llvm::Value *> heap_allocations;
    void prepare_for_early_exit();

    std::string mcpu() const;
    std::string mattrs() const;
};

}}

#endif
