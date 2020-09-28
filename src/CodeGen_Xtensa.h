#ifndef HALIDE_CODEGEN_XTENSA_H
#define HALIDE_CODEGEN_XTENSA_H

/** \file
 * Defines the code-generator for producing Xtensa code
 */

#include "CodeGen_C.h"

namespace Halide {
namespace Internal {

class CodeGen_Xtensa : public CodeGen_C {
public:
    CodeGen_Xtensa(std::ostream &s, Target t, OutputKind output_kind = CImplementation)
        : CodeGen_C(s, t, output_kind) {
    }

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

protected:
    /** Emit the declarations contained in the module as C code. */
    void compile(const LoweredFunc &func) override;
    void compile(const Buffer<> &buffer) override;

    using CodeGen_C::visit;

    bool is_native_vector_type(Type t);

    std::string print_cast_expr(const Type &, const Expr &) override;

    std::string print_xtensa_call(const Call *op);

    void add_vector_typedefs(const std::set<Type> &vector_types) override;

    void visit(const Mul *) override;
    void visit(const Div *) override;

    void visit(const Allocate *) override;
    void visit(const For *) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *op) override;
    void visit(const Call *op) override;
    void visit(const Load *op) override;
    void visit(const Store *op) override;
    void visit(const Select *op) override;
    void visit(const Shuffle *op) override;
    void visit(const Min *op) override;
    void visit(const Max *op) override;

protected:
    int current_loop_level = 0;
    std::vector<std::string> global_static_allocations;
};

}  // namespace Internal
}  // namespace Halide

#endif
