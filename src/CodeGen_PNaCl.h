#ifndef HALIDE_CODEGEN_PNACL_H
#define HALIDE_CODEGEN_PNACL_H

/** \file
 * Defines the code-generator for producing pnacl bitcode.
 */

#include "CodeGen_Posix.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A code generator that emits pnacl bitcode from a given Halide stmt. */
class CodeGen_PNaCl : public CodeGen_Posix {
public:
    /** Create a pnacl code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_PNaCl(Target);

    /** The PNaCl backend overrides compile_to_native to
     * compile_to_bitcode instead. It does *not* run the pnacl
     * sandboxing passes, because these must be run after linking
     * (They change linkage qualifiers on everything, marking
     * everything as internal, including weak symbols that Halide
     * relies on being weak). The final linking stage (e.g. using
     * pnacl-clang++) handles the sandboxing. */
    void compile_to_native(const std::string &filename, bool /*assembly*/) {
        // TODO: Emit .ll when assembly is true
        compile_to_bitcode(filename);
    }

protected:

    using CodeGen_Posix::visit;

    /** Initialize internal state to compile a fresh module. */
    virtual void init_module();

    /* override */ virtual llvm::Triple get_target_triple() const;

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;
};

}}

#endif
