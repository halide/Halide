#ifndef HALIDE_CODEGEN_MIPS_H
#define HALIDE_CODEGEN_MIPS_H

/** \file
 * Defines the code-generator for producing MIPS machine code.
 */

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_MIPS : public CodeGen_Posix {
public:
    /** Create a mips code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_MIPS(Target);

    static void test();

protected:
    using CodeGen_Posix::visit;

    std::string mcpu() const override;
    std::string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
};

}  // namespace Internal
}  // namespace Halide

#endif
