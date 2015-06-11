
#include "Param.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Param.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;

h::Expr imageparam_to_expr_operator0(h::ImageParam &that, p::tuple args_passed)
{
    std::vector<h::Expr> expr_args;
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    // all other variants are equivalent to this one
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
                                  p::init<h::Type, int, std::string>(p::args("self", "t", "dims", "name")))
            .def(p::init<h::Type, int>(p::args("self", "t", "dims")))
            .def("name", &ImageParam::name, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get name of ImageParam.")
            .def("set", &ImageParam::set, p::args("self", "b"),
                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
            .def("get", &ImageParam::get, p::arg("self"),
                 "Get the Buffer that is bound. Only relevant for jitting.")
            .def("__getitem__", &imageparam_to_expr_operator0, p::args("self", "tuple"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit).\n\n"
                 "Call with: [x], [x,y], [x,y,z], or [x,y,z,w]")
            ;

    p::implicitly_convertible<ImageParam, h::Argument>();
    p::implicitly_convertible<ImageParam, h::Expr>();
    return;
}
