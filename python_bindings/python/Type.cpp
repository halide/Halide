#include "Type.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/format.hpp>
#include <boost/python.hpp>

#include "Halide.h"

#include <string>
#include <vector>

namespace h = Halide;

std::string type_code_to_string(const h::Type &t) {
    std::string code_string = "unknown";
    switch (t.code()) {
    case h::Type::UInt:
        code_string = "UInt";
        break;

    case h::Type::Int:
        code_string = "Int";
        break;

    case h::Type::Float:
        code_string = "Float";
        break;

    case h::Type::Handle:
        code_string = "Handle";
        break;

    default:
        code_string = "unknown";
    }

    return code_string;
}

Halide::Type make_handle(int lanes) {
    return Halide::Handle(lanes, nullptr);
}

std::string type_repr(const h::Type &t) {
    auto message_format = boost::format("<halide.Type code '%s' with %i bits and %i lanes>");

    return boost::str(message_format % type_code_to_string(t) % t.bits() % t.lanes());
}

void defineType() {

    using Halide::Type;
    namespace p = boost::python;

    bool (Type::*can_represent_other_type)(Type) const = &Type::can_represent;

    p::class_<Type>("Type",
                    "Default constructor initializes everything to predictable-but-unlikely values",
                    p::no_init)
        .def(p::init<halide_type_code_t, int, int>(p::args("code", "bits", "lanes")))
        .def(p::init<h::Type>(p::args("that"), "Copy constructor"))

        .def("bits", &Type::bits,
             "The number of bits of precision of a single scalar value of this type.")
        .def("bytes", &Type::bytes,
             "The number of bytes required to store a single scalar value of this type. Ignores vector lanes.")
        .def("lanes", &Type::lanes,
             "How many elements (if a vector type). Should be 1 for scalar types.")
        .def("is_bool", &Type::is_bool, p::arg("self"),
             "Is this type boolean (represented as UInt(1))?")
        .def("is_vector", &Type::is_vector, p::arg("self"),
             "Is this type a vector type? (lanes > 1)")
        .def("is_scalar", &Type::is_scalar, p::arg("self"),
             "Is this type a scalar type? (lanes == 1)")
        .def("is_float", &Type::is_float, p::arg("self"),
             "Is this type a floating point type (float or double).")
        .def("is_int", &Type::is_int, p::arg("self"),
             "Is this type a signed integer type?")
        .def("is_uint", &Type::is_uint, p::arg("self"),
             "Is this type an unsigned integer type?")
        .def("is_handle", &Type::is_handle, p::arg("self"),
             "Is this type an opaque handle type (void *)")
        .def(p::self == p::self)
        .def(p::self != p::self)
        .def("with_lanes", &Type::with_lanes, p::args("self", "w"),
             "Produce a copy of this type, with 'lanes' vector lanes")
        .def("with_bits", &Type::with_bits, p::args("self", "w"),
             "Produce a copy of this type, with 'bits' bits")
        .def("element_of", &Type::element_of, p::arg("self"),
             "Produce the type of a single element of this vector type")
        .def("can_represent", can_represent_other_type, p::arg("other"),
             "Can this type represent all values of another type?")
        .def("max", &Type::max, p::arg("self"),
             "Return an expression which is the maximum value of this type")
        .def("min", &Type::min, p::arg("self"),
             "Return an expression which is the minimum value of this type")
        .def("__repr__", &type_repr, p::arg("self"),
             "Return a string containing a printable representation of a Type object.");

    p::def("Int", h::Int,
           (p::arg("bits"), p::arg("lanes") = 1),
           "Constructing an signed integer type");

    p::def("UInt", h::UInt,
           (p::arg("bits"), p::arg("lanes") = 1),
           "Constructing an unsigned integer type");

    p::def("Float", h::Float,
           (p::arg("bits"), p::arg("lanes") = 1),
           "Constructing a floating-point type");

    p::def("Bool", h::Bool,
           (p::arg("lanes") = 1),
           "Construct a boolean type");

    p::def("Handle", make_handle,
           (p::arg("lanes") = 1),
           "Construct a handle type");

    return;
}
