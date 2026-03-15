#ifndef HALIDE_IRGRAPHCXXPRINTER_H
#define HALIDE_IRGRAPHCXXPRINTER_H

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Expr.h"
#include "IR.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

class IRGraphCXXPrinter : public IRVisitor {
public:
    std::ostream &os;

    // Tracks visited nodes so we don't print them twice (handles the DAG structure)
    std::map<const IRNode *, std::string> node_names;
    int var_counter = 0;

    IRGraphCXXPrinter(std::ostream &os) : os(os) {
    }

    void print(const Expr &e) {
        if (e.defined()) {
            e.accept(this);
        }
    }

    void print(const Stmt &s) {
        if (s.defined()) {
            s.accept(this);
        }
    }

private:
    // =========================================================================
    // ✨ CLEVER TEMPLATING ✨
    // This SFINAE trick checks if `T::make` can be invoked with `Args...`.
    // It will trigger a static_assert if you forget an argument or pass the
    // wrong field types, completely preventing generated code compile errors!
    // =========================================================================
    template<typename T, typename... Args>
    static constexpr auto check_make_args(Args &&...args)
        -> decltype(T::make(std::forward<Args>(args)...), std::true_type{}) {
        return std::true_type{};
    }

    template<typename T, typename... Args>
    static constexpr std::false_type check_make_args(...) {
        return std::false_type{};
    }

    // =========================================================================
    // ARGUMENT STRINGIFIERS
    // These convert Halide objects into strings representing C++ code.
    // =========================================================================

    template<typename T>
    std::string to_cpp_arg(const T &x) {
        if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(x);
        } else {
            internal_error << "Not supported to print";
        }
    }

    template<>
    std::string to_cpp_arg<Expr>(const Expr &e) {
        if (!e.defined()) {
            return "Expr()";
        }
        e.accept(this);  // Visit dependencies first
        return node_names.at(e.get());
    }

    std::string to_cpp_arg(const Stmt &s) {
        if (!s.defined()) {
            return "Stmt()";
        }
        s.accept(this);  // Visit dependencies first
        return node_names.at(s.get());
    }

    std::string to_cpp_arg(const Range &r) {
        r.min.accept(this);
        r.extent.accept(this);  // Visit dependencies first
        return "Range(" + node_names.at(r.min.get()) + ", " + node_names.at(r.extent.get()) + ")";
    }

    std::string to_cpp_arg(const std::string &s) {
        return "\"" + s + "\"";
    }

    std::string to_cpp_arg(Type t) {
        std::ostringstream oss;
        oss << "Type(Type::"
            << (t.is_int() ? "Int" : t.is_uint() ? "UInt" :
                                 t.is_float()    ? "Float" :
                                 t.is_bfloat()   ? "BFloat" :
                                                   "Handle")
            << ", " << t.bits() << ", " << t.lanes() << ")";
        return oss.str();
    }

    std::string to_cpp_arg(ForType f) {
        switch (f) {
        case ForType::Serial:
            return "ForType::Serial";
        case ForType::Parallel:
            return "ForType::Parallel";
        case ForType::Vectorized:
            return "ForType::Vectorized";
        case ForType::Unrolled:
            return "ForType::Unrolled";
        case ForType::Extern:
            return "ForType::Extern";
        case ForType::GPUBlock:
            return "ForType::GPUBlock";
        case ForType::GPUThread:
            return "ForType::GPUThread";
        case ForType::GPULane:
            return "ForType::GPULane";
        default:
            return "ForType::Serial";
        }
    }

    std::string to_cpp_arg(const VectorReduce::Operator &op) {
        switch (op) {
        case VectorReduce::Add:
            return "VectorReduce::Add";
        case VectorReduce::SaturatingAdd:
            return "VectorReduce::SaturatingAdd";
        case VectorReduce::Mul:
            return "VectorReduce::Mul";
        case VectorReduce::Min:
            return "VectorReduce::Min";
        case VectorReduce::Max:
            return "VectorReduce::Max";
        case VectorReduce::And:
            return "VectorReduce::And";
        case VectorReduce::Or:
            return "VectorReduce::Or";
        }
        internal_error << "Invalid VectorReduce";
    }

    std::string to_cpp_arg(DeviceAPI api) {
        return "DeviceAPI::" + std::to_string((int)api);  // Or proper switch-case logic
    }

    std::string to_cpp_arg(ModulusRemainder align) {
        return "ModulusRemainder(" + std::to_string(align.modulus) + ", " + std::to_string(align.remainder) + ")";
    }

    std::string to_cpp_arg(const Parameter &p) {
        internal_error << "Not supported to print Parameter";
    }

    template<typename T>
    std::string to_cpp_arg(const std::vector<T> &vec) {
        std::string res = "{";
        for (size_t i = 0; i < vec.size(); ++i) {
            res += to_cpp_arg(vec[i]);
            if (i + 1 < vec.size()) {
                res += ", ";
            }
        }
        res += "}";
        return res;
    }

    // =========================================================================
    // CORE NODE EMITTER
    // =========================================================================
    template<typename T, typename... Args>
    void emit_node(const char *node_type_str, const T *op, Args &&...args) {
        // 1. Maintain DAG properties: if we've already generated it, skip.
        if (node_names.count(op)) {
            return;
        }

        // 2. ✨ Check at our compile-time that the signature aligns exactly!
        static_assert(decltype(check_make_args<T>(std::forward<Args>(args)...))::value,
                      "Arguments extracted for printer do not match any T::make() signature! "
                      "Check your VISIT_NODE macro arguments.");

        // 3. Evaluate arguments post-order to build dependencies.
        // (C++11 guarantees left-to-right evaluation in brace-init lists)
        std::vector<std::string> printed_args = {to_cpp_arg(args)...};

        // 4. Generate the actual C++ code
        bool is_expr = std::is_base_of_v<BaseExprNode, T>;
        std::string var_name = (is_expr ? "expr_" : "stmt_") + std::to_string(var_counter++);

        os << (is_expr ? "Expr " : "Stmt ") << var_name << " = " << node_type_str << "::make(";
        for (size_t i = 0; i < printed_args.size(); ++i) {
            os << printed_args[i] << (i + 1 == printed_args.size() ? "" : ", ");
        }
        os << ");\n";

        node_names[op] = var_name;
    }

protected:
// =========================================================================
// VISITOR OVERRIDES
// =========================================================================

// Macro handles mapping the IR node pointer to our clever template.
#define VISIT_NODE(NodeType, ...)                        \
    void visit(const NodeType *op) override {            \
        IRVisitor::visit(op);                            \
        emit_node<NodeType>(#NodeType, op, __VA_ARGS__); \
    }

    // --- 1. Core / Primitive Values ---
    VISIT_NODE(IntImm, op->type, op->value)
    VISIT_NODE(UIntImm, op->type, op->value)
    VISIT_NODE(FloatImm, op->type, op->value)
    VISIT_NODE(StringImm, op->value)

    // --- 2. Variable & Broadcast ---
    VISIT_NODE(Variable, op->type, op->name /*, op->image, op->param, op->reduction_domain */)
    VISIT_NODE(Broadcast, op->value, op->lanes)

    // --- 3. Binary & Unary Math Nodes ---
    VISIT_NODE(Add, op->a, op->b)
    VISIT_NODE(Sub, op->a, op->b)
    VISIT_NODE(Mod, op->a, op->b)
    VISIT_NODE(Mul, op->a, op->b)
    VISIT_NODE(Div, op->a, op->b)
    VISIT_NODE(Min, op->a, op->b)
    VISIT_NODE(Max, op->a, op->b)
    VISIT_NODE(EQ, op->a, op->b)
    VISIT_NODE(NE, op->a, op->b)
    VISIT_NODE(LT, op->a, op->b)
    VISIT_NODE(LE, op->a, op->b)
    VISIT_NODE(GT, op->a, op->b)
    VISIT_NODE(GE, op->a, op->b)
    VISIT_NODE(And, op->a, op->b)
    VISIT_NODE(Or, op->a, op->b)
    VISIT_NODE(Not, op->a)

    // --- 4. Casts & Shuffles ---
    VISIT_NODE(Cast, op->type, op->value)
    VISIT_NODE(Reinterpret, op->type, op->value)
    VISIT_NODE(Shuffle, op->vectors, op->indices)

    // --- 5. Complex Expressions ---
    VISIT_NODE(Select, op->condition, op->true_value, op->false_value)
    VISIT_NODE(Load, op->type, op->name, op->index, op->image, op->param, op->predicate, op->alignment)
    VISIT_NODE(Ramp, op->base, op->stride, op->lanes)
    VISIT_NODE(Call, op->type, op->name, op->args, op->call_type, op->func, op->value_index, op->image, op->param)
    VISIT_NODE(Let, op->name, op->value, op->body)
    VISIT_NODE(VectorReduce, op->op, op->value, op->type.lanes())

    // --- 6. Core Statements ---
    VISIT_NODE(LetStmt, op->name, op->value, op->body)
    VISIT_NODE(AssertStmt, op->condition, op->message)
    VISIT_NODE(Evaluate, op->value)
    VISIT_NODE(Block, op->first, op->rest)
    VISIT_NODE(IfThenElse, op->condition, op->then_case, op->else_case)
    VISIT_NODE(For, op->name, op->min, op->max, op->for_type, op->partition_policy, op->device_api, op->body)

    // --- 7. Memory / Buffer Operations ---
    VISIT_NODE(Store, op->name, op->value, op->index, op->param, op->predicate, op->alignment)
    VISIT_NODE(Provide, op->name, op->values, op->args, op->predicate)
    VISIT_NODE(Allocate, op->name, op->type, op->memory_type, op->extents, op->condition, op->body, op->new_expr, op->free_function)
    VISIT_NODE(Free, op->name)
    VISIT_NODE(Realize, op->name, op->types, op->memory_type, op->bounds, op->condition, op->body)
    VISIT_NODE(Prefetch, op->name, op->types, op->bounds, op->prefetch, op->condition, op->body)
    VISIT_NODE(HoistedStorage, op->name, op->body)

    // --- 8. Concurrency & Sync ---
    VISIT_NODE(ProducerConsumer, op->name, op->is_producer, op->body)
    VISIT_NODE(Acquire, op->semaphore, op->count, op->body)
    VISIT_NODE(Fork, op->first, op->rest)
    VISIT_NODE(Atomic, op->producer_name, op->mutex_name, op->body)

// Clean up macro
#undef VISIT_NODE

public:
    static void test();
};

}  // namespace Internal

}  // namespace Halide

#endif  // HALIDE_IRGRAPHCXXPRINTER_H
