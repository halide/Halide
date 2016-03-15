#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H
/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

struct Pattern;

/** A code generator that emits Hexagon code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create a Hexagon code generator for the given Hexagon target. */
    CodeGen_Hexagon(Target);

    std::unique_ptr<llvm::Module> compile(const Module &module);

    static void test();

protected:
    // Override begin_func to generate a call to hvx_lock.
    void begin_func(LoweredFunc::LinkageType linkage, const std::string &name,
                    const std::vector<Argument> &args);
    void end_func(const std::vector<Argument> &args);

    using CodeGen_Posix::visit;

    /* /\** Nodes for which we want to emit specific hexagon intrinsics *\/ */
    /* // @{ */
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Broadcast *);
    void visit(const Div *);
    void visit(const Max *);
    void visit(const Min *);
    void visit(const Cast *);
    void visit(const Call *);
    void visit(const Mul *);
    void visit(const Load *);
    void visit(const LE *);
    void visit(const LT *);
    void visit(const NE *);
    void visit(const GT *);
    void visit(const EQ *);
    void visit(const Select *);
    /* // @} */

    bool shouldUseVMPA(const Add *, std::vector<llvm::Value *> &);
    bool shouldUseVDMPY(const Add *, std::vector<llvm::Value *> &);

    llvm::Value *emitBinaryOp(const BaseExprNode *op,
                              std::vector<Pattern> &Patterns);
    llvm::Value *CallLLVMIntrinsic(llvm::Function *F,
                                   std::vector<llvm::Value *> &Ops);
    void getHighAndLowVectors(Expr DoubleVec,
                               std::vector<Expr> &Res);
    void getHighAndLowVectors(llvm::Value *DoubleVec,
                               std::vector<llvm::Value *> &Res);
    llvm::Value *concatVectors(llvm::Value *High, llvm::Value *Low);
    llvm::Value *convertValueType(llvm::Value *V, llvm::Type *T);
    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;
    int bytes_in_vector() const;

 private:
    Expr wild_i32, wild_u32;
    Expr wild_i16, wild_u16;
    Expr wild_i8, wild_u8;
    llvm::Value *getHiVectorFromPair(llvm::Value *Vec);
    llvm::Value *getLoVectorFromPair(llvm::Value *Vec);
    void slice_into_halves(Expr, std::vector<Expr> &);
    llvm::Value *handleLargeVectors(const Add *);
    llvm::Value *handleLargeVectors(const Sub *);
    llvm::Value *handleLargeVectors(const Min *);
    llvm::Value *handleLargeVectors(const Max *);
    llvm::Value *handleLargeVectors(const Mul *);
    llvm::Value *handleLargeVectors(const Div *);
    llvm::Value *handleLargeVectors_absd(const Call *);
    llvm::Value *handleLargeVectors(const Cast *);
    /* Ideally, we'd have liked to call compare with llvm::Intrinsic::ID
     as the last argument, but that means "llvm/IR/Intrinsics.h" would be needed
     to be included here. However, CodeGen_Hexagon.h is used to create Halide.h
     which is needed by the user. All of this would mean that the user would
     need all LLVM Headers. So, we use llvm::Function *F instead, much in the
     same way we do for CallLLVMIntrinsic. */
    llvm::Value *compare(llvm::Value *a, llvm::Value *b,
                         llvm::Function *F);
    llvm::Value *negate(llvm::Value *a);
    llvm::Value *generate_vector_comparison(const BaseExprNode *,
                                            std::vector<Pattern> &,
                                            std::vector<Pattern> &, bool, bool);
    bool possiblyCodeGenWideningMultiply(const Mul *);
    bool possiblyGenerateVMPAAccumulate(const Add *);
    bool possiblyCodeGenNarrowerType(const Select *);
    bool possiblyCodeGenVavg(const Cast *);
    bool possiblyCodeGenSaturatingArith(const Cast *);
    llvm::Value *possiblyCodeGenWideningMultiplySatRndSat(const Div *);
};

}}

#endif
