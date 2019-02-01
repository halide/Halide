#ifndef HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H
#define HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

template <typename other_t, typename PythonClass>
void add_binary_operators_with(PythonClass &class_instance) {
    using self_t = typename PythonClass::type;

#define BINARY_OP(op, method) \
    .def("__" #method "__", [](const self_t &self, const other_t &other) -> decltype(self op other) { return self op other; }, py::is_operator()) \
    .def("__r" #method "__", [](const self_t &self, const other_t &other) -> decltype(self op other) { return other op self; }, py::is_operator())

    class_instance
        BINARY_OP(+, add)
        BINARY_OP(-, sub)
        BINARY_OP(*, mul)
        BINARY_OP(/, div)  // TODO: verify only needed for python 2.x (harmless for Python 3.x)
        BINARY_OP(/, truediv)
        BINARY_OP(%, mod)
        BINARY_OP(<<, lshift)
        BINARY_OP(>>, rshift)
        BINARY_OP(&, and)
        BINARY_OP(|, or)
        BINARY_OP(^, xor)
        BINARY_OP(<, lt)
        BINARY_OP(<=, le)
        BINARY_OP(==, eq)
        BINARY_OP(!=, ne)
        BINARY_OP(>=, ge)
        BINARY_OP(>, gt)
    ;
#undef BINARY_OP

    const auto pow_wrap = [](const self_t &self, const other_t &other) -> decltype(Halide::pow(self, other)) {
        return Halide::pow(self, other);
    };

    const auto floordiv_wrap = [](const self_t &self, const other_t &other) -> decltype(self / other) {
        static_assert(std::is_same<decltype(self / other), Expr>::value, "We expect all operator// overloads to produce Expr");
        Expr e = self / other;
        if (e.type().is_float()) {
            e = Halide::floor(e);
        }
        return e;
    };

    class_instance
        .def("__pow__", pow_wrap, py::is_operator())
        .def("__rpow__", pow_wrap, py::is_operator())
        .def("__floordiv__", floordiv_wrap, py::is_operator())
        .def("__rfloordiv__", floordiv_wrap, py::is_operator())
    ;
}

template <typename PythonClass>
void add_binary_operators(PythonClass &class_instance) {
    using self_t = typename PythonClass::type;

    // The order of definitions matters.
    // Python first will try input value as int, then float, then self_t
    add_binary_operators_with<self_t>(class_instance);
    add_binary_operators_with<Expr>(class_instance);
    add_binary_operators_with<float>(class_instance);
    add_binary_operators_with<int>(class_instance);

    // Define unary operators
    class_instance
        .def(-py::self)  // neg
        .def(~py::self)  // invert
        ;
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBINARYOPERATORS_H
