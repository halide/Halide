#ifndef HALIDE_CODEGEN_ARM_H
#define HALIDE_CODEGEN_ARM_H

/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include "CodeGen_Posix.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_ARM(Target);

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the ARM machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

    llvm::Triple get_target_triple() const;

protected:

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
    void visit(const Store *);
    void visit(const Load *);
    void visit(const Call *);
    // @}

    /** Various patterns to peephole match against */
    struct Pattern {
        std::string intrin; ///< Name of the intrinsic
        int intrin_width;   ///< The native vector width of the intrinsic
        Expr pattern;       ///< The pattern to match against
        enum PatternType {Simple = 0, ///< Just match the pattern
                          LeftShift,  ///< Match the pattern if the RHS is a const power of two
                          RightShift, ///< Match the pattern if the RHS is a const power of two
                          NarrowArgs  ///< Match the pattern if the args can be losslessly narrowed
        };
        PatternType type;
        Pattern() {}
        Pattern(const std::string &i, int w, Expr p, PatternType t = Simple) :
            intrin("llvm.arm.neon." + i), intrin_width(w), pattern(p), type(t) {}
    };
    std::vector<Pattern> casts, left_shifts, averagings, negations;

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

    // On 64-bit ARM, the NEON instrinsics do not work as the syntax
    // changed from 32-bit and this has not been updated.
    // On 32-bit, NEON can be disabled for older processors.
    bool neon_intrinsics_disabled() {
        return target.bits == 64 || target.has_feature(Target::NoNEON);
    }
};

}}

#endif
