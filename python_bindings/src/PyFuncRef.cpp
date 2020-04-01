#include "PyFuncRef.h"

#include <initializer_list>
#include <new>
#include <utility>

#include "Halide.h"
#include "PyBinaryOperators.h"
#include "pybind11/detail/descr.h"
#include "pybind11/pybind11.h"

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
            .def("__len__", &FuncRef::size);

    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
