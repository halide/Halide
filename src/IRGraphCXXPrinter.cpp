#include "IRGraphCXXPrinter.h"

#include "Expr.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {
template<typename T, typename... Args>
static constexpr auto check_make_args(Args &&...args)
    -> decltype(T::make(std::forward<Args>(args)...), std::true_type{}) {
    return std::true_type{};
}

template<typename T, typename... Args>
static constexpr std::false_type check_make_args(...) {
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
template<>
std::string IRGraphCXXPrinter::to_cpp_arg<ForType>(const ForType &f) {
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

template<>
std::string IRGraphCXXPrinter::to_cpp_arg<VectorReduce::Operator>(const VectorReduce::Operator &op) {
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

void IRGraphCXXPrinter::test() {
    // This:
    Expr e = Select::make(Mod::make(Ramp::make(10, 314, 8), Broadcast::make(10, 8)) < Variable::make(Int(32), "p"), Broadcast::make(40, 8) + Ramp::make(4, 8, 8), VectorReduce::make(VectorReduce::Add, Ramp::make(0, 1, 16), 8));

    // Printed by:
    IRGraphCXXPrinter p(std::cout);
    p.print(e);

    // Prints:
    Expr expr_0 = IntImm::make(Type(Type::Int, 32, 1), 10);
    Expr expr_1 = IntImm::make(Type(Type::Int, 32, 1), 314);
    Expr expr_2 = Ramp::make(expr_0, expr_1, 8);
    Expr expr_3 = IntImm::make(Type(Type::Int, 32, 1), 10);
    Expr expr_4 = Broadcast::make(expr_3, 8);
    Expr expr_5 = Mod::make(expr_2, expr_4);
    Expr expr_6 = Variable::make(Type(Type::Int, 32, 1), "p");
    Expr expr_7 = Broadcast::make(expr_6, 8);
    Expr expr_8 = LT::make(expr_5, expr_7);
    Expr expr_9 = IntImm::make(Type(Type::Int, 32, 1), 40);
    Expr expr_10 = Broadcast::make(expr_9, 8);
    Expr expr_11 = IntImm::make(Type(Type::Int, 32, 1), 4);
    Expr expr_12 = IntImm::make(Type(Type::Int, 32, 1), 8);
    Expr expr_13 = Ramp::make(expr_11, expr_12, 8);
    Expr expr_14 = Add::make(expr_10, expr_13);
    Expr expr_15 = IntImm::make(Type(Type::Int, 32, 1), 0);
    Expr expr_16 = IntImm::make(Type(Type::Int, 32, 1), 1);
    Expr expr_17 = Ramp::make(expr_15, expr_16, 16);
    Expr expr_18 = VectorReduce::make(VectorReduce::Add, expr_17, 8);
    Expr expr_19 = Select::make(expr_8, expr_14, expr_18);

    // Now let's see if it matches:
    internal_assert(equal(expr_19, e)) << "Expressions don't match:\n\n"
                                       << e << "\n\n"
                                       << expr_19 << "\n";
}
}  // namespace Internal
}  // namespace Halide
