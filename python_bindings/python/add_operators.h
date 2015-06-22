#ifndef ADD_OPERATORS_H
#define ADD_OPERATORS_H

#include <boost/python/operators.hpp>
#include <boost/python/self.hpp>
//#include <boost/python/def.hpp>


template<typename A, typename B, typename WrappedType>
//WrappedType floordiv(A /*a*/, B /*b*/)
auto floordiv(A a, B b) -> decltype(a / b)
{
    //throw std::invalid_argument("Halide floordiv not yet implemented, use '/' instead.");
    //return WrappedType();

    // Halide does floordiv by default over Expr and similar.
    // see https://lists.csail.mit.edu/pipermail/halide-dev/2015-June/001679.html
    return a / b;
}



template<typename PythonClass, typename T>
void add_operators_with(PythonClass &class_instance)
{
    using namespace boost::python;

    typedef typename PythonClass::wrapped_type wrapped_t;

    class_instance
            .def(self + T())
            .def(T() + self)

            .def(self - T())
            .def(T() - self)

            .def(self * T())
            .def(T() * self)

            .def(self / T())
            .def(T() / self)

            .def(self % T())
            .def(T() % self)

            .def(pow(self, T()))
            .def(pow(T(), self))

            .def(self & T())
            .def(T() & self)

            .def(self | T())
            .def(T() | self)

            .def(self < T())
            .def(T() < self)

            .def(self <= T())
            .def(T() <= self)

            .def(self == T())
            .def(T() == self)

            .def(self != T())
            .def(T() != self)

            .def(self > T())
            .def(T() > self)

            .def(self >= T())
            .def(T() >= self)

            .def("__floordiv__", &floordiv<wrapped_t, T, wrapped_t>)
            .def("__floordiv__", &floordiv<T, wrapped_t, wrapped_t>)

            ;

    return;
}

template<typename PythonClass>
void add_operators(PythonClass &class_instance)
{
    using namespace boost::python;

    // The order of definitions matters.
    // Python first will try input value as int, then float, then as same class (Expr, Var, etc.)
    add_operators_with<PythonClass, int>(class_instance);
    add_operators_with<PythonClass, float>(class_instance);

    class_instance
            .def(self + self)
            .def(self - self)
            .def(self * self)
            .def(self / self)
            .def(self % self)
            //.def(pow(self, p::other<float>))
            .def(pow(self, self))
            .def(self & self) // and
            .def(self | self) // or
            .def(-self) // neg
            .def(~self) // invert
            .def(self < self)
            .def(self <= self)
            .def(self == self)
            .def(self != self)
            .def(self > self)
            .def(self >= self);

    return;
}

#endif // ADD_OPERATORS_H
