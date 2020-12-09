#ifndef HALIDE_CODEGEN_ARM_H
#define HALIDE_CODEGEN_ARM_H

/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include <utility>

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_ARM(Target);

protected:
    using CodeGen_Posix::visit;

    void init_module() override;

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Sub *) override;
    void visit(const Div *) override;
    void visit(const Mul *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const Store *) override;
    void visit(const Load *) override;
    void visit(const Call *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &) override;
    // @}

    /** Various patterns to peephole match against */
    struct Pattern {
        std::string intrin;             ///< Name of the intrinsic
        Expr pattern;                   ///< The pattern to match against
        enum PatternType { Simple = 0,  ///< Just match the pattern
                           LeftShift,   ///< Match the pattern if the RHS is a const power of two
                           RightShift,  ///< Match the pattern if the RHS is a const power of two
                           NarrowArgs   ///< Match the pattern if the args can be losslessly narrowed
        };
        PatternType type;
        Pattern() = default;
        Pattern(const std::string &intrin, Expr p, PatternType t = Simple)
            : intrin(intrin), pattern(std::move(p)), type(t) {
        }
    };
    std::vector<Pattern> casts, averagings, negations, multiplies;

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
