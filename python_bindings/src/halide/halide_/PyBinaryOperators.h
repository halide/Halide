#ifndef HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H
#define HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H

#include "PyFuncRef.h"
#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

#define DEBUG_BINARY_OPS 0

#if DEBUG_BINARY_OPS

inline std::string type_to_str(const Type &t) {
    std::ostringstream o;
    o << "h::" << t;
    return o.str();
}

template<typename T, typename T2 = void>
inline std::string type_desc(const T &v) {
    return "<unknown>";
}

template<>
inline std::string type_desc(const Halide::Expr &v) {
    return "Expr(" + type_to_str(v.type()) + ")";
}

template<>
inline std::string type_desc(const Halide::Var &v) {
    return "Var(" + type_to_str(Int(32)) + ")";
}

template<typename T2>
inline std::string type_desc(const Halide::Param<T2> &v) {
    return "Param(" + type_to_str(Int(32)) + ")";
}

template<>
inline std::string type_desc(const Halide::FuncTupleElementRef &v) {
    return "FuncTupleElementRef(" + type_to_str(v.function().output_types()[0]) + ")";
}

template<>
inline std::string type_desc(const Halide::FuncRef &v) {
    return "FuncRef(" + type_to_str(v.function().output_types()[0]) + ")";
}

#define HANDLE_SCALAR_TYPE(x)                  \
    template<>                                 \
    inline std::string type_desc(const x &v) { \
        return #x;                             \
    }

HANDLE_SCALAR_TYPE(bool)
HANDLE_SCALAR_TYPE(uint8_t)
HANDLE_SCALAR_TYPE(uint16_t)
HANDLE_SCALAR_TYPE(uint32_t)
HANDLE_SCALAR_TYPE(uint64_t)
HANDLE_SCALAR_TYPE(int8_t)
HANDLE_SCALAR_TYPE(int16_t)
HANDLE_SCALAR_TYPE(int32_t)
HANDLE_SCALAR_TYPE(int64_t)
// HANDLE_SCALAR_TYPE(bfloat16_t)  TODO: https://github.com/halide/Halide/issues/6849
HANDLE_SCALAR_TYPE(float16_t)
HANDLE_SCALAR_TYPE(float)
HANDLE_SCALAR_TYPE(double)

#undef HANDLE_SCALAR_TYPE

#define LOG_PY_BINARY_OP(self, op, other, result)                  \
    do {                                                           \
        std::cout << (self) << ":" << type_desc(self) << " "       \
                  << (op)                                          \
                  << " " << (other) << ":" << type_desc(other)     \
                  << " -> "                                        \
                  << (result) << ":" << type_desc(result) << "\n"; \
    } while (0)

#define LOG_PY_BINARY_OP_UNEVAL(self, op, other, result)          \
    do {                                                          \
        std::cout << "undef funcref "(op) " " << (other) << "\n"; \
    } while (0)

#else  // DEBUG_BINARY_OPS

#define LOG_PY_BINARY_OP(self, op, other, result) \
    do {                                          \
    } while (0)

#define LOG_PY_BINARY_OP_UNEVAL(self, op, other, result) \
    do {                                                 \
    } while (0)

#endif  // DEBUG_BINARY_OPS

struct DoubleToExprCheck {
    const Expr e;
    explicit DoubleToExprCheck(double d)
        : e(double_to_expr_check(d)) {
    }
    operator Expr() const {
        return e;
    }
};

template<typename other_t, typename PythonClass>
void add_binary_operators_with(PythonClass &class_instance) {
    using self_t = typename PythonClass::type;
    // If 'other_t' is double, we want to wrap it as an Expr() prior to calling the binary op
    // (so that double literals that lose precision when converted to float issue warnings).
    // For any other type, we just want to leave it as-is.
    using Promote = std::conditional_t<
        std::is_same_v<other_t, double>, DoubleToExprCheck, other_t>;

#define BINARY_OP(op, method)                                   \
    do {                                                        \
        class_instance.def(                                     \
            "__" #method "__",                                  \
            [](const self_t &self, const other_t &other) {      \
                auto result = self op Promote(other);           \
                LOG_PY_BINARY_OP(self, #method, other, result); \
                return result;                                  \
            },                                                  \
            py::is_operator());                                 \
    } while (0)

#define RBINARY_OP(op, method)                                      \
    do {                                                            \
        class_instance.def(                                         \
            "__r" #method "__",                                     \
            [](const self_t &self, const other_t &other) {          \
                auto result = Promote(other) op self;               \
                LOG_PY_BINARY_OP(self, "r" #method, other, result); \
                return result;                                      \
            },                                                      \
            py::is_operator());                                     \
    } while (0)

#define BINARY_OPS(op, method)  \
    do {                        \
        BINARY_OP(op, method);  \
        RBINARY_OP(op, method); \
    } while (0)

    if constexpr (std::is_same_v<self_t, FuncRef>) {
#define BINARY_OPS_UNEVAL(op, method, val)                                                                       \
    do {                                                                                                         \
        class_instance.def(                                                                                      \
            "__" #method "__",                                                                                   \
            [](const self_t &self, const other_t &other) -> std::variant<UnevaluatedFuncRefExpr, Expr> {         \
                if (self.function().has_pure_definition() || self.function().has_extern_definition()) {          \
                    auto result = Expr(self) op Promote(other);                                                  \
                    LOG_PY_BINARY_OP(self, #method, other, result);                                              \
                    return result;                                                                               \
                } else {                                                                                         \
                    auto result = UnevaluatedFuncRefExpr{self, Promote(other), UnevaluatedFuncRefExpr::Op::val}; \
                    LOG_PY_BINARY_OP_UNEVAL(self, #method, other, result);                                       \
                    return result;                                                                               \
                }                                                                                                \
            },                                                                                                   \
            py::is_operator());                                                                                  \
        RBINARY_OP(op, method);                                                                                  \
    } while (0)

        BINARY_OPS_UNEVAL(+, add, Add);
        BINARY_OPS_UNEVAL(-, sub, Sub);
        BINARY_OPS_UNEVAL(*, mul, Mul);
        BINARY_OPS_UNEVAL(/, truediv, Div);
#undef BINARY_OPS_UNEVAL
    } else {
        BINARY_OPS(+, add);
        BINARY_OPS(-, sub);
        BINARY_OPS(*, mul);
        BINARY_OPS(/, truediv);
    }

    BINARY_OPS(%, mod);
    BINARY_OPS(<<, lshift);
    BINARY_OPS(>>, rshift);
    BINARY_OPS(&, and);
    BINARY_OPS(|, or);
    BINARY_OPS(^, xor);
    BINARY_OPS(<, lt);
    BINARY_OPS(<=, le);
    BINARY_OPS(==, eq);
    BINARY_OPS(!=, ne);
    BINARY_OPS(>=, ge);
    BINARY_OPS(>, gt);

#undef BINARY_OP
#undef RBINARY_OP
#undef BINARY_OPS

    const auto floordiv = [](const auto &a, const auto &b) {
        static_assert(std::is_same_v<decltype(a / b), Expr>,
                      "We expect all operator// overloads to produce Expr");
        Expr e = a / b;
        return e.type().is_float() ? Halide::floor(e) : e;
    };

    class_instance
        .def("__floordiv__", [&](const self_t &self, const other_t &other) { return floordiv(self, Promote(other)); }, py::is_operator())
        .def("__rfloordiv__", [&](const self_t &self, const other_t &other) { return floordiv(Promote(other), self); }, py::is_operator());
}  // namespace PythonBindings

template<typename PythonClass>
void add_binary_operators(PythonClass &class_instance) {
    using self_t = typename PythonClass::type;

    // The order of definitions matters.
    // Python first will try input value as int, then double, then self_t
    // (note that we skip 'float' because we should never encounter that in python;
    // all floating-point literals should be double)
    add_binary_operators_with<self_t>(class_instance);
    add_binary_operators_with<Expr>(class_instance);
    add_binary_operators_with<double>(class_instance);
    add_binary_operators_with<int>(class_instance);

    // Halide::pow() has only an Expr, Expr variant
    class_instance
        .def("__pow__", Halide::pow, py::is_operator())
        .def("__rpow__", [](const Expr &self, const Expr &other) {
            return Halide::pow(other, self);  //
        },
             py::is_operator());

    const auto logical_not_wrap = [](const self_t &self) -> decltype(!self) {
        return !self;
    };

    // Define unary operators
    class_instance
        .def(-py::self)  // neg
        .def(~py::self)  // invert
        .def("logical_not", logical_not_wrap);
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H
