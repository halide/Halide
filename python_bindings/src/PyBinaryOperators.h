#ifndef HALIDE_PYTHON_BINDINGS_add_binary_operators_H
#define HALIDE_PYTHON_BINDINGS_add_binary_operators_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

// Note that we deliberately produce different semantics for division in Python3:
// to match Halide C++ division semantics, a signed-integer division is always
// a floordiv rather than a truediv.
template <typename A, typename B>
auto floordiv(A a, B b) -> decltype(a / b) {
    static_assert(std::is_same<decltype(a / b), Expr>::value,
                  "We expect all operator// overloads to produce Expr");
    Expr e = a / b;
    if (e.type().is_float()) {
        e = Halide::floor(e);
    }
    return e;
}

template <typename T, typename PythonClass>
void add_binary_operators_with(PythonClass &class_instance) {
    using wrapped_t = typename PythonClass::wrapped_type;

    class_instance
        .def(py::self + py::other<T>())
        .def(py::other<T>() + py::self)

        .def(py::self - py::other<T>())
        .def(py::other<T>() - py::self)

        .def(py::self * py::other<T>())
        .def(py::other<T>() * py::self)

        .def(py::self / py::other<T>())
        .def(py::other<T>() / py::self)

        .def(py::self % py::other<T>())
        .def(py::other<T>() % py::self)

        .def(pow(py::self, py::other<T>()))
        .def(pow(py::other<T>(), py::self))

        .def(py::self & py::other<T>())  // and
        .def(py::other<T>() & py::self)

        .def(py::self | py::other<T>())  // or
        .def(py::other<T>() | py::self)

        .def(py::self < py::other<T>())
        .def(py::other<T>() < py::self)

        .def(py::self <= py::other<T>())
        .def(py::other<T>() <= py::self)

        .def(py::self == py::other<T>())
        .def(py::other<T>() == py::self)

        .def(py::self != py::other<T>())
        .def(py::other<T>() != py::self)

        .def(py::self > py::other<T>())
        .def(py::other<T>() > py::self)

        .def(py::self >= py::other<T>())
        .def(py::other<T>() >= py::self)

        .def(py::self >> py::other<T>())
        .def(py::other<T>() >> py::self)

        .def(py::self << py::other<T>())
        .def(py::other<T>() << py::self)

        .def("__floordiv__", &floordiv<wrapped_t, T>)
        .def("__floordiv__", &floordiv<T, wrapped_t>)

        ;
}

template <typename PythonClass>
void add_binary_operators(PythonClass &class_instance) {
    using wrapped_t = typename PythonClass::wrapped_type;

    // The order of definitions matters.
    // Python first will try input value as int, then float, then wrapped_t
    add_binary_operators_with<wrapped_t>(class_instance);
    add_binary_operators_with<float>(class_instance);
    add_binary_operators_with<int>(class_instance);

    // Define unary operators
    class_instance
        .def(-py::self)  // neg
        //.def(+py::self) // pos
        .def(~py::self)  // invert
        //.def(abs(py::self))
        //.def(!!py::self) // nonzero
        ;
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_add_binary_operators_H
