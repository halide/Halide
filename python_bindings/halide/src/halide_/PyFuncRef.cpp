#include "PyFuncRef.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

void define_func_ref(py::module &m) {
    auto undefined_lhs_funcref_expr_class =
        py::class_<UnevaluatedFuncRefExpr>(m, "_UnevaluatedFuncRefExpr");

    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);
    add_binary_operators(func_tuple_element_ref_class);

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size)
            .def("type", &FuncRef::type)
            .def("types", &FuncRef::types);
    add_binary_operators(func_ref_class);
}

}  // namespace PythonBindings
}  // namespace Halide
