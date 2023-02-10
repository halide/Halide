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
    CodeGen_Xtensa(std::ostream &s, Target t, OutputKind kind = CImplementation)
        : CodeGen_C(s, t, kind) {
        stack_is_core_private = true;
    }

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

protected:
    /** Emit the declarations contained in the module as C code. */
    void compile(const LoweredFunc &func, const std::map<std::string, std::string> &metadata_name_map) override;
    void compile(const Buffer<> &buffer) override;

    using CodeGen_C::visit;

    std::string print_assignment(Type t, const std::string &rhs) override;
    std::string print_type(Type t, CodeGen_C::AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;
    std::string print_xtensa_call(const Call *op);

    void add_platform_prologue() override;
    void add_vector_typedefs(const std::set<Type> &vector_types) override;

    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;

    void visit(const Allocate *) override;
    void visit(const For *) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *op) override;
    void visit(const Call *op) override;
    void visit(const Cast *op) override;
    void visit(const Load *op) override;
    void visit(const EQ *op) override;
    void visit(const LE *op) override;
    void visit(const LT *op) override;
    void visit(const GE *op) override;
    void visit(const GT *op) override;
    void visit(const Or *op) override;
    void visit(const Reinterpret *op) override;
    void visit(const Store *op) override;
    void visit(const Select *op) override;
    void visit(const Shuffle *op) override;
    void visit(const Min *op) override;
    void visit(const Max *op) override;
    void visit(const IntImm *op) override;
    void visit(const Let *op) override;
    void visit(const LetStmt *op) override;

    int current_loop_level = 0;
    std::vector<std::string> global_static_allocations;

    std::set<std::string> external_buffers;
};

}  // namespace Internal
}  // namespace Halide

#endif
