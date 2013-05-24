#ifndef HALIDE_CODEGEN_ARM_H
#define HALIDE_CODEGEN_ARM_H

/** \file
 * Defines the code-generator for producing ARM machine code 
 */

#include "CodeGen_Posix.h"

namespace Halide { 
namespace Internal {

/** Bitmask flags for specifying code generation options to CodeGen_ARM. */
enum CodeGen_ARM_Options {
    ARM_Android = 1,  /// Compile targetting the Android standard library
    ARM_NaCl    = 2,  /// Compile for Native Client (must be using the Native Client llvm tree)
};

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:
    /** Create an ARM code generator. Processor features can be
     * enabled using the appropriate flags from CodeGen_X86_Options */
    CodeGen_ARM(uint32_t options = 0);
        
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

    /** Compile for Native Client. */
    bool use_nacl;

    /** Generate a call to a neon intrinsic */
    // @{
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>);    
    llvm::Value *call_intrin(llvm::Type *t, const std::string &name, std::vector<llvm::Value *>);    
    void call_void_intrin(const std::string &name, std::vector<Expr>);
    void call_void_intrin(const std::string &name, std::vector<llvm::Value *>);
    // @}

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{    
    void visit(const Cast *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Div *);
    void visit(const Mul *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const Select *);
    void visit(const Store *);
    void visit(const Load *);
    // @}

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
};

}}

#endif
