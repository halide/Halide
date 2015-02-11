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

protected:

    llvm::Triple get_target_triple() const;
    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

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
};

}}

#endif
