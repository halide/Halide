#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H
/** \file
 * Defines the code-generator for producing ARM machine code
 */

#include "CodeGen_Posix.h"

namespace llvm {
namespace Intrinsic {
    enum ID : unsigned;
}
}

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
    using CodeGen_Posix::visit;

    void compile_func(const LoweredFunc &f);

    void init_module();

    llvm::Function *define_hvx_intrinsic(llvm::Intrinsic::ID intrin, Type ret_ty,
                                         const std::string &name,
                                         const std::vector<Type> &arg_types);

    /** Various patterns to peephole match against */
    struct GeneralPattern {
        std::string intrin;   ///< Name of the intrinsic
        Expr pattern;         ///< The pattern to match against
        enum PatternType {Simple = 0, ///< Just match the pattern
                          LeftShift,  ///< Match the pattern if the RHS is a const power of two
                          RightShift, ///< Match the pattern if the RHS is a const power of two
                          NarrowArgs  ///< Match the pattern if the args can be losslessly narrowed
        };
        PatternType type;
        GeneralPattern() {}
        GeneralPattern(const std::string &intrin, Expr p, PatternType t = Simple)
            : intrin(intrin), pattern(p), type(t) {}
    };
    std::vector<GeneralPattern> casts, adds, subs;

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
    void visit(const GE *);
    void visit(const LE *);
    void visit(const LT *);
    void visit(const NE *);
    void visit(const GT *);
    void visit(const EQ *);
    void visit(const Select *);
    /* // @} */

    bool shouldUseVMPA(const Add *, std::vector<llvm::Value *> &);
    bool shouldUseVDMPY(const Add *, std::vector<llvm::Value *> &);

    llvm::Value *emitBinaryOp(const BaseExprNode *op, std::vector<Pattern> &Patterns);
    llvm::Value *callLLVMIntrinsic(llvm::Function *F, std::vector<llvm::Value *> Ops);
    llvm::Value *callLLVMIntrinsic(llvm::Intrinsic::ID id, std::vector<llvm::Value *> Ops);
    std::vector<Expr> getHighAndLowVectors(Expr DoubleVec);
    std::vector<llvm::Value *> getHighAndLowVectors(llvm::Value *DoubleVec);
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

    // These are wildcards with 1x, 2x, and 4x the HVX vector width per the target flags.
    Expr wild_u8xW, wild_i8xW;
    Expr wild_u16xW, wild_i16xW;
    Expr wild_u32xW, wild_i32xW;
    Expr wild_u8x2W, wild_i8x2W;
    Expr wild_u16x2W, wild_i16x2W;
    Expr wild_u32x2W, wild_i32x2W;
    Expr wild_u8x4W, wild_i8x4W;
    Expr wild_u16x4W, wild_i16x4W;
    Expr wild_u32x4W, wild_i32x4W;

    /** Define overloads of CodeGen_LLVM::call_intrin that use Intrinsic::ID. */
    ///@{
    using CodeGen_LLVM::call_intrin;
    llvm::Value *call_intrin(Type t, const std::string &name, std::vector<Expr>);
    llvm::Value *call_intrin(llvm::Type *t, const std::string &name, std::vector<llvm::Value *>);
    llvm::Value *call_intrin(Type t, int intrin_lanes,
                             llvm::Intrinsic::ID id,
                             int ops_lanes, std::vector<Expr>);
    llvm::Value *call_intrin(llvm::Type *t, int intrin_lanes,
                             llvm::Intrinsic::ID id,
                             int ops_lanes, std::vector<llvm::Value *>);
    ///@}

    llvm::Value *getHiVectorFromPair(llvm::Value *Vec);
    llvm::Value *getLoVectorFromPair(llvm::Value *Vec);
    std::vector<Expr> slice_into_halves(Expr);
    llvm::Value *handleLargeVectors(const Add *);
    llvm::Value *handleLargeVectors(const Sub *);
    llvm::Value *handleLargeVectors(const Mul *);
    llvm::Value *handleLargeVectors(const Div *);
    llvm::Value *handleLargeVectors(const Cast *);
    bool possiblyCodeGenWideningMultiply(const Mul *);
    bool possiblyGenerateVMPAAccumulate(const Add *);
    bool possiblyCodeGenNarrowerType(const Select *);
    llvm::Value *possiblyCodeGenWideningMultiplySatRndSat(const Div *);
};

}}

#endif
