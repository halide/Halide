#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

Type make_handle(int lanes) {
    return Handle(lanes, nullptr);
}

std::string type_repr(const Type &t) {
    std::ostringstream o;
    o << "<halide.Type " << halide_type_to_string(t) << ">";
    return o.str();
}

}  // namespace

std::string halide_type_to_string(const Type &type) {
    std::ostringstream stream;
    if (type.code() == halide_type_uint && type.bits() == 1) {
        stream << "bool";
    } else {
        switch (type.code()) {
        case halide_type_int:
            stream << "int";
            break;
        case halide_type_uint:
            stream << "uint";
            break;
        case halide_type_float:
            stream << "float";
            break;
        case halide_type_handle:
            stream << "handle";
            break;
        default:
            stream << "#unknown";
            break;
        }
        stream << std::to_string(type.bits());
    }
    if (type.lanes() > 1) {
        stream << "x" + std::to_string(type.lanes());
    }
    return stream.str();
}

void define_type(py::module &m) {
    py::class_<Type>(m, "Type")
        .def(py::init<>())
        .def(py::init<halide_type_code_t, int, int>(), py::arg("code"), py::arg("bits"), py::arg("lanes"))
        .def("bytes", &Type::bytes)
        .def("code", &Type::code)
        .def("bits", &Type::bits)
        .def("lanes", &Type::lanes)
        .def("with_code", &Type::with_code, py::arg("code"))
        .def("with_bits", &Type::with_bits, py::arg("bits"))
        .def("with_lanes", &Type::with_lanes, py::arg("lanes"))

        .def("is_bool", &Type::is_bool)
        .def("is_vector", &Type::is_vector)
        .def("is_scalar", &Type::is_scalar)
        .def("is_float", &Type::is_float)
        .def("is_int", &Type::is_int)
        .def("is_uint", &Type::is_uint)
        .def("is_handle", &Type::is_handle)
        .def("same_handle_type", &Type::same_handle_type, py::arg("other"))

        .def("__eq__", [](const Type &value, Type *value2) -> bool { return value2 && value == *value2; })
        .def("__ne__", [](const Type &value, Type *value2) -> bool { return !value2 || value != *value2; })

        // This is defined in C++ so that Types can live in std::map, but it's not clear that
        // it's useful for the Python bindings; leave it out for now.
        // .def("__lt__", [](const Type &value, Type *value2) -> bool { return value2 && value < *value2; })

        .def("element_of", &Type::element_of)
        .def("can_represent", (bool(Type::*)(Type) const) & Type::can_represent, py::arg("other"))
        // Python doesn't have unsigned integers -- all integers are signed --
        // so we'll never see anything that can usefully be routed to the uint64_t
        // overloads of these methods.
        .def("is_max", (bool(Type::*)(int64_t) const) & Type::is_max, py::arg("value"))
        .def("is_min", (bool(Type::*)(int64_t) const) & Type::is_min, py::arg("value"))
        .def("max", &Type::max)
        .def("min", &Type::min)
        .def("__repr__", &type_repr)
        .def("__str__", &halide_type_to_string);

    m.def("Int", Int, py::arg("bits"), py::arg("lanes") = 1);
    m.def("UInt", UInt, py::arg("bits"), py::arg("lanes") = 1);
    m.def("Float", Float, py::arg("bits"), py::arg("lanes") = 1);
    m.def("Bool", Bool, py::arg("lanes") = 1);
    m.def("Handle", make_handle, py::arg("lanes") = 1);
}

}  // namespace PythonBindings
}  // namespace Halide
