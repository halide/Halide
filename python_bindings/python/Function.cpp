#include "Function.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include <vector>

void defineExternFuncArgument() {
    using Halide::ExternFuncArgument;
    namespace h = Halide;
    namespace p = boost::python;

    p::class_<ExternFuncArgument>("ExternFuncArgument",
                                  "An argument to an extern-defined Func. May be a Function, Buffer, "
                                  "ImageParam or Expr.",
                                  p::no_init)
        .def(p::init<h::Buffer<>>(p::args("self", "b")))
        .def(p::init<h::Expr>(p::args("self", "e")))
        .def(p::init<int>(p::args("self", "e")))
        .def(p::init<float>(p::args("self", "e")))

        .def_readwrite("arg_type", &ExternFuncArgument::arg_type)
        .def_readwrite("buffer", &ExternFuncArgument::buffer)
        .def_readwrite("expr", &ExternFuncArgument::expr)

        .def("is_func", &ExternFuncArgument::is_func)
        .def("is_expr", &ExternFuncArgument::is_expr)
        .def("is_buffer", &ExternFuncArgument::is_buffer)
        .def("is_image_param", &ExternFuncArgument::is_image_param)
        .def("defined", &ExternFuncArgument::defined);

    return;
}
