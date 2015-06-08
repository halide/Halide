#include "Type.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Type.h"


#include <string>

void defineType()
{

    namespace h = Halide;
    namespace p = boost::python;
    using p::self;

    p::class_<h::Type>("Type", p::no_init);

    p::def("Int", h::Int,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing an signed integer type");

    p::def("UInt", h::UInt,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing an unsigned integer type");

    p::def("Float", h::Float,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing a floating-point type");

    p::def("Bool", h::Bool,
           (p::arg("width")=1),
           "Construct a boolean type");

    return;
}


