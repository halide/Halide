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
        std::string intrin32; ///< Name of the intrinsic for 32-bit arm
        std::string intrin64; ///< Name of the intrinsic for 64-bit arm
        int intrin_width;     ///< The native vector width of the intrinsic
        Expr pattern;         ///< The pattern to match against
        enum PatternType {Simple = 0, ///< Just match the pattern
                          LeftShift,  ///< Match the pattern if the RHS is a const power of two
                          RightShift, ///< Match the pattern if the RHS is a const power of two
                          NarrowArgs  ///< Match the pattern if the args can be losslessly narrowed
        };
        PatternType type;
        Pattern() {}
        Pattern(const std::string &i32, const std::string &i64, int w, Expr p, PatternType t = Simple) :
            intrin32("llvm.arm.neon." + i32),
            intrin64("llvm.aarch64.neon." + i64),
            intrin_width(w), pattern(p), type(t) {}
    };
    std::vector<Pattern> casts, left_shifts, averagings, negations;

    // Call an intrinsic as defined by a pattern. Dispatches to the
    // 32- or 64-bit name depending on the target's bit width.
    // @{
    llvm::Value *call_pattern(const Pattern &p, Type t, const std::vector<Expr> &args);
    llvm::Value *call_pattern(const Pattern &p, llvm::Type *t, const std::vector<llvm::Value *> &args);
    // @}

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

    // NEON can be disabled for older processors.
    bool neon_intrinsics_disabled() {
        return target.has_feature(Target::NoNEON);
    }
};

}}

#endif
