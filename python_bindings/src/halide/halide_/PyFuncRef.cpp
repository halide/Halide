#include "PyFuncRef.h"

#include "PyBinaryOperators.h"
#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

void define_func_ref(py::module &m) {
    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);

    add_binary_operators(func_tuple_element_ref_class);

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size)
            /*
             * The __iadd__ implementations here are meant to implement the special handling
             * of update definitions like the following:
             *
             *     Func f; RDom r(...);
             *     f(r) += ...;
             *
             * In this case, the type of the zero for f's pure definition is inferred from the
             * RHS. FuncRef's overloads of operator+= return a Stage. However, in Python, if
             * the __iadd__ (+=) method returns a new object, then __setitem__ will be called
             * to update the underlying object. In this case, it would try to call
             *
             *     f.__setitem__(r, <Stage>)
             *
             * where <Stage> is the result of FuncRef::operator+=. This doesn't make sense.
             * Instead, we return the same FuncRef. The same logic applies to the other
             * in-place overloads.
             */
            // __iadd__
            .def("__iadd__", [](FuncRef &self, const Expr &other) {
                self += other;
                return self;
            })
            .def("__iadd__", [](FuncRef &self, const py::tuple &other) {
                self += to_halide_tuple(other);
                return self;
            })
            .def("__iadd__", [](FuncRef &self, const FuncRef &other) {
                self += other;
                return self;
            })
            // __isub__
            .def("__isub__", [](FuncRef &self, const Expr &other) {
                self -= other;
                return self;
            })
            .def("__isub__", [](FuncRef &self, const py::tuple &other) {
                self -= to_halide_tuple(other);
                return self;
            })
            .def("__isub__", [](FuncRef &self, const FuncRef &other) {
                self -= other;
                return self;
            })
            // __imul__
            .def("__imul__", [](FuncRef &self, const Expr &other) {
                self *= other;
                return self;
            })
            .def("__imul__", [](FuncRef &self, const py::tuple &other) {
                self *= to_halide_tuple(other);
                return self;
            })
            .def("__imul__", [](FuncRef &self, const FuncRef &other) {
                self *= other;
                return self;
            })
            // __idiv__
            .def("__itruediv__", [](FuncRef &self, const Expr &other) {
                self /= other;
                return self;
            })
            .def("__itruediv__", [](FuncRef &self, const py::tuple &other) {
                self /= to_halide_tuple(other);
                return self;
            })
            .def("__itruediv__", [](FuncRef &self, const FuncRef &other) {
                self /= other;
                return self;
            });

    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
