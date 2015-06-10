
#include "Param.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Param.h"

#include <string>

void defineParam()
{
    // Might create linking problems, if Param.cpp is not included in the python library

    using Halide::ImageParam;
    namespace h = Halide;
    namespace p = boost::python;
    using p::self;

    p::class_<ImageParam>("ImageParam",
                          "An Image parameter to a halide pipeline. E.g., the input image. \n"
                          "Constructor:: \n"
                          "  ImageParam(Type t, int dims, name="") \n"
                          "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. Supports most of \n"
                          "the methods of Image.",
                          p::init<h::Type, int, std::string>(p::args("t", "dims", "name"))
                          )
            .def(p::init<h::Type, int>(p::args("t", "dims")))
            .def("name",
                 &ImageParam::name,
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get name of ImageParam.")
            .def("set", &ImageParam::set, p::arg("b"),
                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
            .def("get", &ImageParam::get,
                 "Get the Buffer that is bound. Only relevant for jitting.");
    return;
}
