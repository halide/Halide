#include "PyFuncRef.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

void define_func_ref(py::module_ &m) {
    auto func_tuple_element_ref_class =
        py::class_<FuncTupleElementRef>(m, "FuncTupleElementRef")
            .def("index", &FuncTupleElementRef::index);

#if !HALIDE_USE_NANOBIND
    // TODO
    add_binary_operators(func_tuple_element_ref_class);
#endif

    auto func_ref_class =
        py::class_<FuncRef>(m, "FuncRef")
            .def("__getitem__", &FuncRef::operator[])
            .def("size", &FuncRef::size)
            .def("__len__", &FuncRef::size);

#if !HALIDE_USE_NANOBIND
    // TODO
    add_binary_operators(func_ref_class);
#endif
}

}  // namespace PythonBindings
}  // namespace Halide
