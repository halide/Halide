#ifndef ADD_OPERATORS_H
#define ADD_OPERATORS_H

#include <boost/python/operators.hpp>
#include <boost/python/self.hpp>
//#include <boost/python/def.hpp>

template<typename PythonClass>
void add_operators(PythonClass &class_instance)
{
    using namespace boost::python;

    // FIXME Var + int, Var + float not yet working
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
