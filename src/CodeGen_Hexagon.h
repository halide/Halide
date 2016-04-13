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

/** A code generator that emits Hexagon code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create a Hexagon code generator for the given Hexagon target. */
    CodeGen_Hexagon(Target);

    std::unique_ptr<llvm::Module> compile(const Module &module);

protected:
    void compile_func(const LoweredFunc &f,
                      const std::string &simple_name, const std::string &extern_name);

    void init_module();

    Expr mulhi_shr(Expr a, Expr b, int shr);
    Expr sorted_avg(Expr a, Expr b);

    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

    llvm::Function *define_hvx_intrinsic(llvm::Intrinsic::ID intrin, Type ret_ty,
                                         const std::string &name,
                                         const std::vector<Type> &arg_types,
                                         bool broadcast_scalar_word = false);
    llvm::Function *define_hvx_intrinsic(llvm::Function *intrin, Type ret_ty,
                                         const std::string &name,
                                         const std::vector<Type> &arg_types,
                                         bool broadcast_scalar_word = false);

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
    void visit(const GE *);
    void visit(const LE *);
    void visit(const LT *);
    void visit(const NE *);
    void visit(const GT *);
    void visit(const EQ *);
    void visit(const Select *);
    /* // @} */

    /** Call an LLVM intrinsic, potentially casting the operands to
     * match the type of the function. */
    ///@{
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, llvm::Function *F,
                                  std::vector<llvm::Value *> Ops);
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, llvm::Intrinsic::ID id,
                                  std::vector<llvm::Value *> Ops);
    ///@}

    /** Define overloads of CodeGen_LLVM::call_intrin that determine
     * the intrin_lanes from the type, and allows the function to
     * return null if the maybe option is true and the intrinsic is
     * not found. */
    ///@{
    using CodeGen_LLVM::call_intrin;
    llvm::Value *call_intrin(Type t, const std::string &name,
                             std::vector<Expr>, bool maybe = false);
    llvm::Value *call_intrin(llvm::Type *t, const std::string &name,
                             std::vector<llvm::Value *>, bool maybe = false);
    ///@}

    /** Override CodeGen_LLVM to use hexagon intrinics when possible. */
    ///@{
    llvm::Value *interleave_vectors(Type type, const std::vector<Expr> &v);
    llvm::Value *slice_vector(llvm::Value *vec, int start, int size);
    llvm::Value *concat_vectors(const std::vector<llvm::Value *> &v);
    ///@}
};

}}

#endif
