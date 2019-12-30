#include "PyFuncRef.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

void define_func_ref(py::module &m) {
    auto func_tuple_element_ref_class = py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
                                            .def("index", &FuncTupleElementRef::index);

    add_binary_operators_with<Expr>(func_tuple_element_ref_class);

    auto func_ref_class = py::class_<FuncRef>(m, "FuncRef")
                              .def("__getitem__", &FuncRef::operator[])
                              .def("size", &FuncRef::size)
                              .def("__len__", &FuncRef::size);

    add_binary_operators_with<Expr>(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
