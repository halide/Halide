#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H

/** \file
 * Defines the code-generator for producing Hexagon machine code
 */

#include "CodeGen_Posix.h"

namespace Halide {
namespace Internal {

/** A code generator that emits Hexagon code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create a Hexagon code generator for the given Hexagon target. */
    CodeGen_Hexagon(Target);

protected:
    void compile_func(const LoweredFunc &f,
                      const std::string &simple_name, const std::string &extern_name) override;

    void init_module() override;

    std::string mcpu() const override;
    std::string mattrs() const override;
    int isa_version;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    llvm::Function *define_hvx_intrinsic(llvm::Function *intrin, Type ret_ty,
                                         const std::string &name,
                                         std::vector<Type> arg_types,
                                         int flags);

    int is_hvx_v62_or_later() {
        return (isa_version >= 62);
    }
    int is_hvx_v65_or_later() {
        return (isa_version >= 65);
    }

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific hexagon intrinsics */
    ///@{
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Broadcast *) override;
    void visit(const Div *) override;
    void visit(const Max *) override;
    void visit(const Min *) override;
    void visit(const Cast *) override;
    void visit(const Call *) override;
    void visit(const Mul *) override;
    void visit(const GE *) override;
    void visit(const LE *) override;
    void visit(const LT *) override;
    void visit(const NE *) override;
    void visit(const GT *) override;
    void visit(const EQ *) override;
    void visit(const Select *) override;
    void visit(const Allocate *) override;
    ///@}

    /** We ask for an extra vector on each allocation to enable fast
     * clamped ramp loads. */
    int allocation_padding(Type type) const override {
        return CodeGen_Posix::allocation_padding(type) + native_vector_bits() / 8;
    }

    /** Call an LLVM intrinsic, potentially casting the operands to
     * match the type of the function. */
    ///@{
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, llvm::Function *F,
                                  std::vector<llvm::Value *> Ops);
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, int id,
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
    llvm::Value *interleave_vectors(const std::vector<llvm::Value *> &v) override;
    llvm::Value *shuffle_vectors(llvm::Value *a, llvm::Value *b,
                                 const std::vector<int> &indices) override;
    using CodeGen_Posix::shuffle_vectors;
    ///@}

    /** Generate a LUT lookup using vlut instructions. */
    ///@{
    llvm::Value *vlut(llvm::Value *lut, llvm::Value *indices, int min_index = 0, int max_index = 1 << 30);
    llvm::Value *vlut(llvm::Value *lut, const std::vector<int> &indices);
    ///@}

    llvm::Value *vdelta(llvm::Value *lut, const std::vector<int> &indices);

    /** Because HVX intrinsics operate on vectors of i32, using them
     * requires a lot of extraneous bitcasts, which make it difficult
     * to manipulate the IR. This function avoids generating redundant
     * bitcasts. */
    llvm::Value *create_bitcast(llvm::Value *v, llvm::Type *ty);

private:
    /** Generates code for computing the size of an allocation from a
     * list of its extents and its size. Fires a runtime assert
     * (halide_error) if the size overflows 2^31 -1, the maximum
     * positive number an int32_t can hold. */
    llvm::Value *codegen_cache_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents);
};

}  // namespace Internal
}  // namespace Halide

#endif
