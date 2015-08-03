#include "Function.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "no_compare_indexing_suite.h"

#include "../../src/Func.h" // includes everything needed here

#include <vector>

void defineExternFuncArgument()
{
    using Halide::ExternFuncArgument;
    namespace h = Halide;
    namespace p = boost::python;

    p::class_<ExternFuncArgument>("ExternFuncArgument",
                                  "An argument to an extern-defined Func. May be a Function, Buffer, "
                                  "ImageParam or Expr.",
                                  p::no_init)
            //ExternFuncArgument(Internal::IntrusivePtr<Internal::FunctionContents> f)
            .def(p::init<h::Buffer>(p::args("self", "b")))
            .def(p::init<h::Expr>(p::args("self", "e")))
            .def(p::init<int>(p::args("self", "e")))
            .def(p::init<float>(p::args("self", "e")))
            //ExternFuncArgument(Internal::Parameter p) // Scalar params come in via the Expr constructor.

            .def_readwrite("arg_type", &ExternFuncArgument::arg_type)
            //.def_readwrite("func", &ExternFuncArgument::func) // Internal::IntrusivePtr<Internal::FunctionContents>
            .def_readwrite("buffer", &ExternFuncArgument::buffer)
            .def_readwrite("expr", &ExternFuncArgument::expr)
            //.def_readwrite("image_param", &ExternFuncArgument::image_param) // Internal::Parameter

            .def("is_func", &ExternFuncArgument::is_func)
            .def("is_expr", &ExternFuncArgument::is_expr)
            .def("is_buffer", &ExternFuncArgument::is_buffer)
            .def("is_image_param", &ExternFuncArgument::is_image_param)
            .def("defined", &ExternFuncArgument::defined)
            ;

    p::class_< std::vector<ExternFuncArgument> >("ExternFuncArgumentsVector")
            .def( no_compare_indexing_suite< std::vector<ExternFuncArgument> >() );
    return;
}
