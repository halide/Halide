#ifndef HALIDE_CODEGEN_MIPS_H
#define HALIDE_CODEGEN_MIPS_H

/** \file
 * Defines the code-generator for producing MIPS machine code.
 */

#include "CodeGen_Posix.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_MIPS : public CodeGen_Posix {
public:
    /** Create a mips code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_MIPS(Target);

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the mips machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

    static void test();

protected:

    llvm::Triple get_target_triple() const;

    using CodeGen_Posix::visit;

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
};

}}

#endif
