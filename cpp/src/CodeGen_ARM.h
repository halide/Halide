#ifndef HALIDE_CODEGEN_ARM_H
#define HALIDE_CODEGEN_ARM_H

/** \file
 * Defines the code-generator for producing ARM machine code 
 */

#include "CodeGen_Posix.h"

namespace Halide { 
namespace Internal {

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:

    /** Create an ARM code generator. Processor features can be
     * enabled using the appropriate arguments */
    CodeGen_ARM(bool android = false);
        
    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the ARM machine
     * code. */
    void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);

    static void test();

protected:

    /** Use the android-specific standard library */
    bool use_android;

    /** Generate a call to a neon intrinsic */
    // @{
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>);    
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<llvm::Value *>);    
    // @}

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{    
    void visit(const Cast *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Div *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const Select *);
    // @}

    std::string mcpu() const;
    std::string mattrs() const;
};

}}

#endif
