#include "IRGraphCXXPrinter.h"

#include "Expr.h"
#include "Function.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"

#include <sstream>

namespace Halide {
namespace Internal {

namespace {
template<typename T, typename... Args>
constexpr auto check_make_args(Args &&...args)
    -> decltype(T::make(std::forward<Args>(args)...), std::true_type{}) {
    return std::true_type{};
}

template<typename T, typename... Args>
constexpr std::false_type check_make_args(...) {
    return std::false_type{};
}

}  // namespace

template<typename T>
std::string IRGraphCXXPrinter::to_cpp_arg(const T &x) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(x);
    } else {
        internal_error << "Not supported to print";
    }
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Expr>(const Expr &e) {
    if (!e.defined()) {
        return "Expr()";
    }
    include(e);
    return node_names.at(e.get());
}

// Not used, but leaving in place in case we ever want to expand this to Stmts.
template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Stmt>(const Stmt &s) {
    if (!s.defined()) {
        return "Stmt()";
    }
    include(s);
    return node_names.at(s.get());
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Range>(const Range &r) {
    include(r.min);
    include(r.extent);
    return "Range(" + node_names.at(r.min.get()) + ", " + node_names.at(r.extent.get()) + ")";
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<std::string>(const std::string &s) {
    return "\"" + s + "\"";
}

#define ENUM_TO_STR(x) \
    case x:            \
        return #x;

// Not used, but leaving in place in case we ever want to expand this to Stmts.
template<>
std::string IRGraphCXXPrinter::to_cpp_arg<ForType>(const ForType &f) {
    switch (f) {
        ENUM_TO_STR(ForType::Serial);
        ENUM_TO_STR(ForType::Parallel);
        ENUM_TO_STR(ForType::Vectorized);
        ENUM_TO_STR(ForType::Unrolled);
        ENUM_TO_STR(ForType::Extern);
        ENUM_TO_STR(ForType::GPUBlock);
        ENUM_TO_STR(ForType::GPUThread);
        ENUM_TO_STR(ForType::GPULane);
    }
    return "";
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Call::CallType>(const Call::CallType &f) {
    switch (f) {
        ENUM_TO_STR(Call::CallType::Image);
        ENUM_TO_STR(Call::CallType::Extern);
        ENUM_TO_STR(Call::CallType::ExternCPlusPlus);
        ENUM_TO_STR(Call::CallType::PureExtern);
        ENUM_TO_STR(Call::CallType::Halide);
        ENUM_TO_STR(Call::CallType::Intrinsic);
        ENUM_TO_STR(Call::CallType::PureIntrinsic);
    }
    return "";
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<VectorReduce::Operator>(const VectorReduce::Operator &op) {
    switch (op) {
        ENUM_TO_STR(VectorReduce::Add);
        ENUM_TO_STR(VectorReduce::SaturatingAdd);
        ENUM_TO_STR(VectorReduce::Mul);
        ENUM_TO_STR(VectorReduce::Min);
        ENUM_TO_STR(VectorReduce::Max);
        ENUM_TO_STR(VectorReduce::And);
        ENUM_TO_STR(VectorReduce::Or);
    }
    return "";
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Halide::Parameter>(const Halide::Parameter &p) {
    if (!p.defined()) {
        return "Parameter()";
    }
    return "/* Parameter */ " + to_cpp_arg(p.name());
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Halide::Buffer<>>(const Halide::Buffer<> &b) {
    if (!b.defined()) {
        return "Buffer<>()";
    }
    return "/* Buffer */ " + to_cpp_arg(b.name());
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<Type>(const Type &t) {
    std::ostringstream oss;
    oss << "Type(Type::"
        << (t.is_int() ? "Int" : t.is_uint() ? "UInt" :
                             t.is_float()    ? "Float" :
                             t.is_bfloat()   ? "BFloat" :
                                               "Handle")
        << ", " << t.bits() << ", " << t.lanes() << ")";
    return oss.str();
}

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<ModulusRemainder>(const ModulusRemainder &align) {
    return "ModulusRemainder(" + std::to_string(align.modulus) + ", " + std::to_string(align.remainder) + ")";
}

template<typename T>
std::string IRGraphCXXPrinter::to_cpp_arg(const std::vector<T> &vec) {
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

template<typename T, typename... Args>
void IRGraphCXXPrinter::emit_node(const char *node_type_str, const T *op, Args &&...args) {
    if (node_names.count(op)) {
        return;
    }

    static_assert(decltype(check_make_args<T>(std::forward<Args>(args)...))::value,
                  "Arguments extracted for printer do not match any T::make() signature! "
                  "Check your VISIT_NODE macro arguments.");

    // Evaluate arguments post-order to build dependencies.
    // (C++11 guarantees left-to-right evaluation in brace-init lists)
    std::vector<std::string> printed_args = {to_cpp_arg(args)...};

    // Generate the actual C++ code
    bool is_expr = std::is_base_of_v<BaseExprNode, T>;
    std::string var_name = (is_expr ? "expr_" : "stmt_") + std::to_string(var_counter++);

    os << (is_expr ? "Expr " : "Stmt ") << var_name << " = " << node_type_str << "::make(";
    for (size_t i = 0; i < printed_args.size(); ++i) {
        os << printed_args[i] << (i + 1 == printed_args.size() ? "" : ", ");
    }
    os << ");\n";

    node_names[op] = var_name;
}

// Macro handles mapping the IR node pointer to our clever template.
#define VISIT_NODE(NodeType, ...)                        \
    void IRGraphCXXPrinter::visit(const NodeType *op) {  \
        IRGraphVisitor::visit(op);                       \
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

void IRGraphCXXPrinter::visit(const Call *op) {
    if (op->call_type == Call::Image && op->image.defined()) {
        // Variant 4: Convenience constructor for loads from concrete images
        emit_node<Call>("Call", op, op->image, op->args);
    } else if (op->call_type == Call::Image && op->param.defined()) {
        // Variant 5: Convenience constructor for loads from image parameters
        emit_node<Call>("Call", op, op->param, op->args);
    } else if (op->call_type == Call::Halide && op->func.defined()) {
        // Variant 3: Convenience constructor for calls to other halide functions.
        // We wrap the FunctionPtr into a Function object to perfectly match
        // the expected `const Function &func` signature.
        emit_node<Call>("Call", op, Internal::Function(op->func), op->args, op->value_index);
    } else if (op->is_intrinsic()) {

        emit_node<Call>("Call", op, op->type, op->name, op->args, op->call_type);
    } else {
        // Variant 2: Fallback to the fully explicit string-name version.
        // (Note: Halide's API internally handles mapping string names back
        // to IntrinsicOp if it happens to be an intrinsic call).
        emit_node<Call>("Call", op, op->type, op->name, op->args, op->call_type,
                        op->func, op->value_index, op->image, op->param);
    }
}

VISIT_NODE(Let, op->name, op->value, op->body)
VISIT_NODE(VectorReduce, op->op, op->value, op->type.lanes())

#if 0  // Currently no support yet for Stmts, however, the macros below are already correct. We just can't print everything yet.
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
#endif

#undef ENUM_TO_STR

void IRGraphCXXPrinter::test() {
#define STR(X) #X "\n"
#define CODE(X) X
    {
        // This:
        Expr e = Select::make(Mod::make(Ramp::make(10, 314, 8), Broadcast::make(10, 8)) < Variable::make(Int(32), "p"), Broadcast::make(40, 8) + Ramp::make(4, 8, 8), VectorReduce::make(VectorReduce::Add, Ramp::make(0, 1, 16), 8));
        e = e * e;  // make it a graph
        e = cast(Float(32, 8), e);
        e = reinterpret(Int(32, 8), e);
        e = Shuffle::make_interleave({e, e * Broadcast::make(3, 8)});

        // Printed by:
        std::stringstream ss;
        IRGraphCXXPrinter p(ss);
        p.print(e);

        // Prints:
#define RESULT(X)                                                         \
    X(Expr expr_0 = IntImm::make(Type(Type::Int, 32, 1), 10);)            \
    X(Expr expr_1 = IntImm::make(Type(Type::Int, 32, 1), 314);)           \
    X(Expr expr_2 = Ramp::make(expr_0, expr_1, 8);)                       \
    X(Expr expr_3 = IntImm::make(Type(Type::Int, 32, 1), 10);)            \
    X(Expr expr_4 = Broadcast::make(expr_3, 8);)                          \
    X(Expr expr_5 = Mod::make(expr_2, expr_4);)                           \
    X(Expr expr_6 = Variable::make(Type(Type::Int, 32, 1), "p");)         \
    X(Expr expr_7 = Broadcast::make(expr_6, 8);)                          \
    X(Expr expr_8 = LT::make(expr_5, expr_7);)                            \
    X(Expr expr_9 = IntImm::make(Type(Type::Int, 32, 1), 40);)            \
    X(Expr expr_10 = Broadcast::make(expr_9, 8);)                         \
    X(Expr expr_11 = IntImm::make(Type(Type::Int, 32, 1), 4);)            \
    X(Expr expr_12 = IntImm::make(Type(Type::Int, 32, 1), 8);)            \
    X(Expr expr_13 = Ramp::make(expr_11, expr_12, 8);)                    \
    X(Expr expr_14 = Add::make(expr_10, expr_13);)                        \
    X(Expr expr_15 = IntImm::make(Type(Type::Int, 32, 1), 0);)            \
    X(Expr expr_16 = IntImm::make(Type(Type::Int, 32, 1), 1);)            \
    X(Expr expr_17 = Ramp::make(expr_15, expr_16, 16);)                   \
    X(Expr expr_18 = VectorReduce::make(VectorReduce::Add, expr_17, 8);)  \
    X(Expr expr_19 = Select::make(expr_8, expr_14, expr_18);)             \
    X(Expr expr_20 = Mul::make(expr_19, expr_19);)                        \
    X(Expr expr_21 = Cast::make(Type(Type::Float, 32, 8), expr_20);)      \
    X(Expr expr_22 = Reinterpret::make(Type(Type::Int, 32, 8), expr_21);) \
    X(Expr expr_23 = IntImm::make(Type(Type::Int, 32, 1), 3);)            \
    X(Expr expr_24 = Broadcast::make(expr_23, 8);)                        \
    X(Expr expr_25 = Mul::make(expr_22, expr_24);)                        \
    X(Expr expr_26 = Shuffle::make({expr_22, expr_25}, {0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15});)

        std::string expected = RESULT(STR);
        internal_assert(expected == ss.str()) << "Generated C++ code was not as expected."
                                              << "Expected:\n"
                                              << expected << "\n\nGenerated:\n"
                                              << ss.str() << "\n";

        // Now let's see if the IR it produces matches:
        RESULT(CODE);
        const Expr &printed = expr_26;
        internal_assert(equal(printed, e)) << "Expressions don't match:\n\n"
                                           << e << "\n\n"
                                           << printed << "\n";
#undef RESULT
    }

    {
        // An expression Alex was interested in:
        Expr imm1 = IntImm::make(Int(16), -32000);
        Expr imm2 = UIntImm::make(UInt(16), 1);
        Expr cast_imm1 = Cast::make(UInt(64), imm1);
        Expr cast_imm2 = Cast::make(UInt(64), imm2);
        Expr test_cast = ~(cast_imm1 / cast_imm2);

        // Printed by:
        std::stringstream ss;
        IRGraphCXXPrinter p(ss);
        p.print(test_cast);

        // Produces:
#define RESULT(X)                                                  \
    X(Expr expr_0 = IntImm::make(Type(Type::Int, 16, 1), -32000);) \
    X(Expr expr_1 = Cast::make(Type(Type::UInt, 64, 1), expr_0);)  \
    X(Expr expr_2 = UIntImm::make(Type(Type::UInt, 16, 1), 1);)    \
    X(Expr expr_3 = Cast::make(Type(Type::UInt, 64, 1), expr_2);)  \
    X(Expr expr_4 = Div::make(expr_1, expr_3);)                    \
    X(Expr expr_5 = Call::make(Type(Type::UInt, 64, 1), "bitwise_not", {expr_4}, Call::CallType::PureIntrinsic);)

        std::string expected = RESULT(STR);
        internal_assert(expected == ss.str()) << "Generated C++ code was not as expected."
                                              << "Expected:\n"
                                              << expected << "\n\nGenerated:\n"
                                              << ss.str() << "\n";

        // Now let's see if it matches:
        RESULT(CODE);
        const Expr &printed = expr_5;
        internal_assert(equal(printed, test_cast)) << "Expressions don't match:\n\n"
                                                   << test_cast << "\n\n"
                                                   << printed << "\n";
#undef RESULT
    }
}
}  // namespace Internal
}  // namespace Halide
