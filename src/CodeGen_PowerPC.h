#ifndef HALIDE_CODEGEN_POWERPC_H
#define HALIDE_CODEGEN_POWERPC_H

/** \file
 * Defines the code-generator for producing POWERPC machine code.
 */

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_PowerPC : public CodeGen_Posix {
public:
    /** Create a powerpc code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_PowerPC(Target);

    static void test();

protected:

    std::string mcpu() const override;
    std::string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    // @}

    // Call an intrinsic as defined by a pattern. Dispatches to the
private:
    static const char *altivec_int_type_name(const Type &);
};

}  // namespace Internal
}  // namespace Halide

#endif
