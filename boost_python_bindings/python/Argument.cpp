#include "Argument.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>

#include "../../src/Argument.h"

#include <string>

namespace h = Halide;
std::string argument_name(h::Argument &that)
{
    return that.name;
}

h::Argument::Kind argument_kind(h::Argument &that)
{
    return that.kind;
}

bool operator ==(const h::Argument &a, const h::Argument &b)
{
    return a.name == b.name;
}


void defineArgument()
{

    using Halide::Argument;
    namespace p = boost::python;
    using p::self;

    auto argument_class =
            p::class_<Argument>("Argument",
                                "A struct representing an argument to a halide-generated function. "
                                "Used for specifying the function signature of generated code.",
                                p::init<>());
    auto init_keywords =
            (p::arg("name"), p::arg("kind"), p::arg("type"), p::arg("dimensions")=0,
             // using h::Expr initialization creates a run-time exception
             //(p::arg("default")=h::Expr()), (p::arg("min")=h::Expr()), (p::arg("max")=h::Expr())
             p::arg("default"), p::arg("min"), p::arg("max")
             );

    argument_class.def(p::init<std::string, Argument::Kind, h::Type, uint8_t, h::Expr, h::Expr, h::Expr>(init_keywords));

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
            .export_values()
            ;


    argument_class
            //.property("kind", &Argument::kind)
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
            //.def("dimensions", [](Argument &that){ return that.dimensions;},
            .def_readonly("kind", &Argument::kind,
                          "If kind == InputBuffer|OutputBuffer, this is the dimensionality of the buffer. "
                          "If kind == InputScalar, this value is ignored (and should always be set to zero)");

    argument_class
            //.def("type", [](Argument &that){ return that.type; },
            .def_readonly("type", &Argument::type,
                          "If this is a scalar parameter, then this is its type. "
                          " If this is a buffer parameter, this is used to determine elem_size of the buffer_t. "
                          "Note that type.width should always be 1 here.");


    argument_class
            //.def("default", [](Argument &that){ return that.def; },
            .def_readonly("def", &Argument::def,
                          "If this is a scalar parameter, then these are its default, min, max values. "
                          "By default, they are left unset, implying \"no default, no min, no max\". ")
            //.def("min", [](Argument &that){ return that.min; })
            .def_readonly("min", &Argument::min)
            //.def("max", [](Argument &that){ return that.max; })
            .def_readonly("max", &Argument::max);


    argument_class
            .def("is_buffer", &Argument::is_buffer,
                 "An argument is either a primitive type (for parameters), or a buffer pointer. "
                 "If 'is_buffer' is true, then 'type' should be ignored.")
            .def("is_scalar", &Argument::is_scalar)
            .def("is_input", &Argument::is_input)
            .def("is_output", &Argument::is_output);

    /*
            p::class_< std::vector<h::Argument> >("ArgumentsVector")
                    .def( p::vector_indexing_suite< std::vector<h::Argument> >() );
        */
    return;
}
