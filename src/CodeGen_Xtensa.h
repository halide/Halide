#ifndef HALIDE_CODEGEN_XTENSA_H
#define HALIDE_CODEGEN_XTENSA_H

/** \file
 * Defines the code-generator for producing Xtensa code
 */

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CodeGen_C.h"

namespace Halide {
namespace Internal {

class CodeGen_Xtensa : public CodeGen_C {
public:
    CodeGen_Xtensa(std::ostream &dest,
                   const Target &target,
                   OutputKind output_kind = CImplementation,
                   const std::string &include_guard = "");

protected:
    Stmt preprocess_function_body(const Stmt &stmt) override;

    using CodeGen_C::visit;

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

    template<typename ComparisonOp>
    void visit_comparison_op(const ComparisonOp *op, std::string_view op_name);

    bool is_stack_private_to_thread() const override;

    void emit_halide_free_helper(const std::string &alloc_name, const std::string &free_function) override;

    int current_loop_level = 0;
    std::vector<std::string> global_static_allocations;

    // TODO: this appears to be unused; we read from it but never write to it?
    std::set<std::string> external_buffers;

    // ---------------------
    // For most of our purposes, a halide_type_t is just as good as a Halide::Type,
    // but notably smaller and more efficient (since it fits into a u32 and hashes well).
    class HalideTypeHasher {
    public:
        size_t operator()(const halide_type_t &t) const;
    };

    using HalideTypeSet = std::unordered_set<halide_type_t, HalideTypeHasher>;

    // A set of enums representing all possible 'xtensa native' vector types.
    // Note that this is a subset of all the Halide scalar types, since Xtensa
    // doesn't support all of these as vectors. The 'none' type is for cases
    // in which the type we have doesn't match any of the Xtensa native vectors
    // (ie, the 'default:' case).
    //
    // You'd normally use halide_type_to_xnv_type_map to map a specific `op->type`
    // to one of these, and use these as the cases for a switch or similar.
    // (Note that we can't use the expected u32 value of the op->type since
    // those will have varying lanes depending on what our Target is set to.)
    enum class XNVType {
        none = 0,

        float16,
        float32,
        int8,
        int16,
        int24,
        int32,
        int48,
        int64,
        uint8,
        uint16,
        uint24,
        uint32,
        uint48,
    };

    using HalideTypeToXNVMap = std::unordered_map<halide_type_t, XNVType, HalideTypeHasher>;
    const HalideTypeToXNVMap halide_type_to_xnv_type_map;

    // like `halide_type_to_xnv_type_map[op_type]`, but return XNVType::none if not found.
    XNVType xnv_type(const halide_type_t &op_type) const;

    void print_custom_binop(std::string_view name, const Type &t, const Expr &a, const Expr &b);

    template<typename T>
    bool is_native_xtensa_vector(halide_type_t op_type) const {
        constexpr halide_type_t cpp_type = halide_type_of<T>();
        return op_type == cpp_type.with_lanes(target.natural_vector_size<T>());
    }

    template<>
    bool is_native_xtensa_vector<int64_t>(halide_type_t op_type) const {
        constexpr halide_type_t cpp_type = halide_type_of<int64_t>();
        // On Xtensa int64 vectors are *wide* vectors, so the number of lanes match
        // the number of lanes for 32-bit vectors.
        return op_type == cpp_type.with_lanes(target.natural_vector_size<int32_t>());
    }

    halide_type_t get_native_xtensa_vector(const halide_type_t &t) const;

    bool is_native_vector_type(const halide_type_t &t) const {
        return t == get_native_xtensa_vector(t);
    }

    bool is_double_native_vector_type(const halide_type_t &t) const {
        const halide_type_t native_vector_type = get_native_xtensa_vector(t);
        return t == native_vector_type.with_lanes(2 * native_vector_type.lanes);
    }

    const std::unordered_map<std::string, std::string> op_name_to_intrinsic;
};

}  // namespace Internal
}  // namespace Halide

#endif
