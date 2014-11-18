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

    static void test();

protected:

    llvm::Triple get_target_triple() const;

    /** Generate a call to a neon intrinsic */
    // @{
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>);
    llvm::Value *call_intrin(llvm::Type *t, const std::string &name, std::vector<llvm::Value *>);
    llvm::Instruction *call_void_intrin(const std::string &name, std::vector<Expr>);
    llvm::Instruction *call_void_intrin(const std::string &name, std::vector<llvm::Value *>);
    // @}

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
    void visit(const LT *);
    void visit(const LE *);
    void visit(const Select *);
    void visit(const Store *);
    void visit(const Load *);
    void visit(const Call *);
    // @}

    /** Various patterns to peephole match against */
    struct Pattern {
        std::string intrin;
        Expr pattern;
        enum PatternType {Simple = 0, LeftShift, RightShift, NarrowArgs};
        PatternType type;
        Pattern() {}
        Pattern(std::string i, Expr p, PatternType t = Simple) : intrin(i), pattern(p), type(t) {}
    };
    std::vector<Pattern> casts, left_shifts, averagings, negations;


    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
};

}}

#endif
