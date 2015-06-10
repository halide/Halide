
#include "Param.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Param.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;

h::Expr imageparam_to_expr_operator0(h::ImageParam &that)
{
    return that();
}

h::Expr imageparam_to_expr_operator1(h::ImageParam &that, h::Expr x)
{
    return that(x);
}

h::Expr imageparam_to_expr_operator2(h::ImageParam &that, h::Expr x, h::Expr y)
{
    return that(x,y);
}

h::Expr imageparam_to_expr_operator3(h::ImageParam &that, h::Expr x, h::Expr y, h::Expr z)
{
    return that(x,y,z);
}

h::Expr imageparam_to_expr_operator4(h::ImageParam &that, h::Expr x, h::Expr y, h::Expr z, h::Expr w)
{
    return that(x,y,z,w);
}


h::Expr imageparam_to_expr_operator5(h::ImageParam &that, std::vector<h::Expr> args_passed)
{
    return that(args_passed);
}

h::Expr imageparam_to_expr_operator6(h::ImageParam &that, std::vector<h::Var> args_passed)
{
    return that(args_passed);
}


h::Expr imageparam_to_expr_operator00(h::ImageParam &that, p::tuple args_passed)
{
    std::vector<h::Expr> expr_args;
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    const size_t args_len = p::len(args_passed);
    for(size_t i=0; i < args_len; i+=1)
    {
        expr_args.push_back(p::extract<h::Expr>(args_passed[i]));
    }

    return that(expr_args);
}


void defineParam()
{
    // Might create linking problems, if Param.cpp is not included in the python library

    using Halide::ImageParam;

    using p::self;

    auto image_param_class =
            p::class_<ImageParam>("ImageParam",
                                  "An Image parameter to a halide pipeline. E.g., the input image. \n"
                                  "Constructor:: \n"
                                  "  ImageParam(Type t, int dims, name="") \n"
                                  "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. "
                                  "Supports most of the methods of Image.",
                                  p::init<h::Type, int, std::string>(p::args("t", "dims", "name")))
            .def(p::init<h::Type, int>(p::args("t", "dims")))
            .def("name",
                 &ImageParam::name,
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get name of ImageParam.")
            .def("set", &ImageParam::set, p::arg("b"),
                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
            .def("get", &ImageParam::get,
                 "Get the Buffer that is bound. Only relevant for jitting.");


    const std::string imageparam_to_expr_doc = \
            "Construct an expression which loads from this image. "
            "The location is extended with enough implicit variables to match "
            "the dimensionality of the image (see \ref Var::implicit)";

    image_param_class
            .def("__getitem__", &imageparam_to_expr_operator00, p::args("self", "tuple"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator0, p::args("self"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator1, p::args("self", "x"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator2, p::args("self", "x", "y"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator3, p::args("self", "x", "y", "z"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator4, p::args("self", "x", "y", "z", "w"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator5, p::args("self", "args_passed"),
                 imageparam_to_expr_doc.c_str())
            .def("__getitem__", &imageparam_to_expr_operator6, p::args("self", "args_passed"),
                 imageparam_to_expr_doc.c_str())
            ;

    p::implicitly_convertible<ImageParam, h::Argument>();
    p::implicitly_convertible<ImageParam, h::Expr>();
    return;
}
