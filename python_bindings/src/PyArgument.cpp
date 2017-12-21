#include "PyArgument.h"

namespace Halide {
namespace PythonBindings {

void define_argument() {
    auto argument_class =
        py::class_<Argument>("Argument",
                            "A struct representing an argument to a halide-generated function. "
                            "Used for specifying the function signature of generated code.",
                            py::init<>(py::arg("self")));

    argument_class
        .def(py::init<std::string, Argument::Kind, Type, uint8_t, Expr, Expr, Expr>(
            (py::arg("self"), py::arg("name"), py::arg("kind"), py::arg("type"), py::arg("dimensions"),
             py::arg("default"), py::arg("min"), py::arg("max"))))
        .def(py::init<std::string, Argument::Kind, Type, uint8_t, Expr>(
            (py::arg("self"), py::arg("name"), py::arg("kind"), py::arg("type"), py::arg("dimensions"),
             py::arg("default"))))
        .def(py::init<std::string, Argument::Kind, Type, uint8_t>(
            (py::arg("self"), py::arg("name"), py::arg("kind"), py::arg("type"), py::arg("dimensions"))));

    argument_class
        .def_readonly("name", &Argument::name, "The name of the argument.");

    py::enum_<Argument::Kind>("ArgumentKind")
        .value("InputScalar", Argument::Kind::InputScalar)
        .value("InputBuffer", Argument::Kind::InputBuffer)
        .value("OutputBuffer", Argument::Kind::OutputBuffer)
        .export_values();

    argument_class
        .def_readonly("kind", &Argument::kind,
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
        .def("is_buffer", &Argument::is_buffer, py::arg("self"),
             "An argument is either a primitive type (for parameters), or a buffer pointer. "
             "If 'is_buffer' is true, then 'type' should be ignored.")
        .def("is_scalar", &Argument::is_scalar, py::arg("self"))
        .def("is_input", &Argument::is_input, py::arg("self"))
        .def("is_output", &Argument::is_output, py::arg("self"));
}

}  // namespace PythonBindings
}  // namespace Halide
