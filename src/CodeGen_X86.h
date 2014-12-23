#ifndef HALIDE_CODEGEN_X86_H
#define HALIDE_CODEGEN_X86_H

/** \file
 * Defines the code-generator for producing x86 machine code
 */

#include "CodeGen_Posix.h"
#include "Target.h"

namespace llvm {
class JITEventListener;
}

namespace Halide {
namespace Internal {

/** A code generator that emits x86 code from a given Halide stmt. */
class CodeGen_X86 : public CodeGen_Posix {
public:
    /** Create an x86 code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_X86(Target);

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86 machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

    static void test();

    void jit_init(llvm::ExecutionEngine *, llvm::Module *);
    void jit_finalize(llvm::ExecutionEngine *, llvm::Module *, std::vector<JITCompiledModule::CleanupRoutine> *);

protected:

    llvm::Triple get_target_triple() const;

    /** Generate a call to an sse or avx intrinsic */
    // @{
    llvm::Value *call_intrin(Type t, int w, const std::string &name, std::vector<Expr>);
    llvm::Value *call_intrin(llvm::Type *t, int w, const std::string &name, std::vector<llvm::Value *>);
    // @}

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    // @{
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Cast *);
    void visit(const Div *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const GT *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GE *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const Select *);
    // @}

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

private:
    llvm::JITEventListener* jitEventListener;
};

}}

#endif
