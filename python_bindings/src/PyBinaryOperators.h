#ifndef HALIDE_PYTHON_BINDINGS_add_binary_operators_H
#define HALIDE_PYTHON_BINDINGS_add_binary_operators_H

#include <boost/python/operators.hpp>
#include <boost/python/self.hpp>

#include "Halide.h"

// Note that we deliberately produce different semantics for division in Python3:
// to match Halide C++ division semantics, a signed-integer division is always
// a floordiv rather than a truediv.
template <typename A, typename B>
auto floordiv(A a, B b) -> decltype(a / b) {
    static_assert(std::is_same<decltype(a / b), Halide::Expr>::value,
                  "We expect all operator// overloads to produce Halide::Expr");
    Halide::Expr e = a / b;
    if (e.type().is_float()) {
        e = Halide::floor(e);
    }
    return e;
}

template <typename T, typename PythonClass>
void add_binary_operators_with(PythonClass &class_instance) {
    using namespace boost::python;

    using wrapped_t = typename PythonClass::wrapped_type;

    // <boost/python/operators.hpp> lists all operators
    class_instance
        .def(self + other<T>())
        .def(other<T>() + self)

        .def(self - other<T>())
        .def(other<T>() - self)

        .def(self * other<T>())
        .def(other<T>() * self)

        .def(self / other<T>())
        .def(other<T>() / self)

        .def(self % other<T>())
        .def(other<T>() % self)

        .def(pow(self, other<T>()))
        .def(pow(other<T>(), self))

        .def(self & other<T>())  // and
        .def(other<T>() & self)

        .def(self | other<T>())  // or
        .def(other<T>() | self)

        .def(self < other<T>())
        .def(other<T>() < self)

        .def(self <= other<T>())
        .def(other<T>() <= self)

        .def(self == other<T>())
        .def(other<T>() == self)

        .def(self != other<T>())
        .def(other<T>() != self)

        .def(self > other<T>())
        .def(other<T>() > self)

        .def(self >= other<T>())
        .def(other<T>() >= self)

        .def(self >> other<T>())
        .def(other<T>() >> self)

        .def(self << other<T>())
        .def(other<T>() << self)

        .def("__floordiv__", &floordiv<wrapped_t, T>)
        .def("__floordiv__", &floordiv<T, wrapped_t>)

        ;
}

template <typename PythonClass>
void add_binary_operators(PythonClass &class_instance) {
    using namespace boost::python;

    using wrapped_t = typename PythonClass::wrapped_type;

    // The order of definitions matters.
    // Python first will try input value as int, then float, then wrapped_t
    add_binary_operators_with<wrapped_t>(class_instance);
    add_binary_operators_with<float>(class_instance);
    add_binary_operators_with<int>(class_instance);

    // Define unary operators
    // <boost/python/operators.hpp> lists all operators
    class_instance
        .def(-self)  // neg
        //.def(+self) // pos
        .def(~self)  // invert
        //.def(abs(self))
        //.def(!!self) // nonzero
        ;
}

#endif  // HALIDE_PYTHON_BINDINGS_add_binary_operators_H
