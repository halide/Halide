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
    /** Create an ARM code generator for the given arm target. */
    CodeGen_ARM(Target);

protected:

    Expr sorted_avg(Expr a, Expr b) override;

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Div *) override;
    void visit(const Mul *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const Store *) override;
    void visit(const Load *) override;
    void visit(const Call *) override;
    // @}

    /** Various patterns to peephole match against */
    struct Pattern {
        std::string intrin32; ///< Name of the intrinsic for 32-bit arm
        std::string intrin64; ///< Name of the intrinsic for 64-bit arm
        int intrin_lanes;     ///< The native vector width of the intrinsic
        Expr pattern;         ///< The pattern to match against
        enum PatternType {Simple = 0, ///< Just match the pattern
                          LeftShift,  ///< Match the pattern if the RHS is a const power of two
                          RightShift, ///< Match the pattern if the RHS is a const power of two
                          NarrowArgs  ///< Match the pattern if the args can be losslessly narrowed
        };
        PatternType type;
        Pattern() {}
        Pattern(const std::string &i32, const std::string &i64, int l, Expr p, PatternType t = Simple) :
            intrin32("llvm.arm.neon." + i32),
            intrin64("llvm.aarch64.neon." + i64),
            intrin_lanes(l), pattern(p), type(t) {}
    };
    std::vector<Pattern> casts, left_shifts, averagings, negations;

    // Call an intrinsic as defined by a pattern. Dispatches to the
    // 32- or 64-bit name depending on the target's bit width.
    // @{
    llvm::Value *call_pattern(const Pattern &p, Type t, const std::vector<Expr> &args);
    llvm::Value *call_pattern(const Pattern &p, llvm::Type *t, const std::vector<llvm::Value *> &args);
    // @}

    std::string mcpu() const override;
    std::string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    // NEON can be disabled for older processors.
    bool neon_intrinsics_disabled() {
        return target.has_feature(Target::NoNEON);
    }
};

}  // namespace Internal
}  // namespace Halide

#endif
