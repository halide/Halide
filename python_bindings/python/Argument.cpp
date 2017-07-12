#include "Argument.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include <string>

namespace h = Halide;

void defineArgument() {
    using Halide::Argument;
    namespace p = boost::python;

    auto argument_class =
        p::class_<Argument>("Argument",
                            "A struct representing an argument to a halide-generated function. "
                            "Used for specifying the function signature of generated code.",
                            p::init<>(p::arg("self")));

    argument_class
        .def(p::init<std::string, Argument::Kind, h::Type, uint8_t, h::Expr, h::Expr, h::Expr>(
            (p::arg("self"), p::arg("name"), p::arg("kind"), p::arg("type"), p::arg("dimensions"),
             p::arg("default"), p::arg("min"), p::arg("max"))))
        .def(p::init<std::string, Argument::Kind, h::Type, uint8_t, h::Expr>(
            (p::arg("self"), p::arg("name"), p::arg("kind"), p::arg("type"), p::arg("dimensions"),
             p::arg("default"))))
        .def(p::init<std::string, Argument::Kind, h::Type, uint8_t>(
            (p::arg("self"), p::arg("name"), p::arg("kind"), p::arg("type"), p::arg("dimensions"))));

    argument_class
        .def_readonly("name", &Argument::name, "The name of the argument.");
    //.property("name", &Argument::name, "The name of the argument.")
    //.def("name",
    //     &argument_name, // getter instead of property to be consistent with other parts of the API
    //     "The name of the argument.");

    p::enum_<Argument::Kind>("ArgumentKind")
        .value("InputScalar", Argument::Kind::InputScalar)
        .value("InputBuffer", Argument::Kind::InputBuffer)
        .value("OutputBuffer", Argument::Kind::OutputBuffer)
        .export_values();

    argument_class
        //.def("kind", &argument_kind,
        .def_readonly("kind", &Argument::kind,
                      //.def("kind", [](Argument &that) -> Argument::Kind { return that.kind; },
                      //.def("kind", std::function<Argument::Kind(Argument &)>( [](Argument &that) { return that.kind; } ),
                      "An argument is either a primitive type (for parameters), or a buffer pointer.\n"
                      "If kind == InputScalar, then type fully encodes the expected type of the scalar argument."
                      "If kind == InputBuffer|OutputBuffer, then type.bytes() should be used "
                      "to determine* elem_size of the buffer; additionally, type.code *should* "
                      "reflect the expected interpretation of the buffer data (e.g. float vs int), "
                      "but there is no runtime enforcement of this at present.");

    argument_class
        .def_readonly("dimensions", &Argument::dimensions,
                      "If kind == InputBuffer|OutputBuffer, this is the dimensionality of the buffer. "
                      "If kind == InputScalar, this value is ignored (and should always be set to zero)");

    argument_class
        .def_readonly("type", &Argument::type,
                      "If this is a scalar parameter, then this is its type. "
                      " If this is a buffer parameter, this is used to determine elem_size of the buffer_t. "
                      "Note that type.width should always be 1 here.");

    argument_class
        .def_readonly("default", &Argument::def,
                      "If this is a scalar parameter, then these are its default, min, max values. "
                      "By default, they are left unset, implying \"no default, no min, no max\". ")
        .def_readonly("min", &Argument::min)
        .def_readonly("max", &Argument::max);

    argument_class
        .def("is_buffer", &Argument::is_buffer, p::arg("self"),
             "An argument is either a primitive type (for parameters), or a buffer pointer. "
             "If 'is_buffer' is true, then 'type' should be ignored.")
        .def("is_scalar", &Argument::is_scalar, p::arg("self"))
        .def("is_input", &Argument::is_input, p::arg("self"))
        .def("is_output", &Argument::is_output, p::arg("self"));

    return;
}
