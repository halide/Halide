#include "PyFunction.h"

namespace Halide {
namespace PythonBindings {

void define_extern_func_argument() {
    py::class_<ExternFuncArgument>("ExternFuncArgument",
                                  "An argument to an extern-defined Func. May be a Function, Buffer, "
                                  "ImageParam or Expr.",
                                  py::no_init)
        .def(py::init<Buffer<>>(py::args("self", "b")))
        .def(py::init<Expr>(py::args("self", "e")))
        .def(py::init<int>(py::args("self", "e")))
        .def(py::init<float>(py::args("self", "e")))

        .def_readwrite("arg_type", &ExternFuncArgument::arg_type)
        .def_readwrite("buffer", &ExternFuncArgument::buffer)
        .def_readwrite("expr", &ExternFuncArgument::expr)

        .def("is_func", &ExternFuncArgument::is_func)
        .def("is_expr", &ExternFuncArgument::is_expr)
        .def("is_buffer", &ExternFuncArgument::is_buffer)
        .def("is_image_param", &ExternFuncArgument::is_image_param)
        .def("defined", &ExternFuncArgument::defined);
}

}  // namespace PythonBindings
}  // namespace Halide
