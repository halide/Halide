#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H
/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

struct Pattern;

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_Hexagon(Target);

    llvm::Triple get_target_triple() const;
    llvm::DataLayout get_data_layout() const;

    static void test();

protected:


    using CodeGen_Posix::visit;

    /* /\** Nodes for which we want to emit specific neon intrinsics *\/ */
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
    /* // @} */

    bool shouldUseVMPA(const Add *, std::vector<llvm::Value *> &);
    bool shouldUseVDMPY(const Add *, std::vector<llvm::Value *> &);

    llvm::Value *emitBinaryOp(const BaseExprNode *op,
                              std::vector<Pattern> &Patterns);
    llvm::Value *CallLLVMIntrinsic(llvm::Function *F,
                                   std::vector<llvm::Value *> &Ops);
    void getHighAndLowVectors(llvm::Value *DoubleVec,
                               std::vector<llvm::Value *> &Res);
    llvm::Value *concatVectors(llvm::Value *High, llvm::Value *Low);
    llvm::Value *convertValueType(llvm::Value *V, llvm::Type *T);
    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;
 private:
    Expr wild_i32, wild_u32;
    Expr wild_i16, wild_u16;
    llvm::Value *getHiVectorFromPair(llvm::Value *Vec);
    llvm::Value *getLoVectorFromPair(llvm::Value *Vec);
    void slice_into_halves(Expr, std::vector<Expr> &);
    llvm::Value *handleLargeVectors(const Div *);
    llvm::Value *handleLargeVectors(const Add *);
    llvm::Value *handleLargeVectors(const Mul *);
    llvm::Value *handleLargeVectors(const Cast *);

};

}}

#endif
