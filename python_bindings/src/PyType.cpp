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

void define_type() {
    bool (Type::*can_represent_method)(Type) const = &Type::can_represent;
    // Python doesn't have unsigned integers -- all integers are signed --
    // so we'll never see anything that can usefully be routed to the uint64_t
    // overloads of these methods.
    bool (Type::*is_max_i)(int64_t) const = &Type::is_max;
    bool (Type::*is_min_i)(int64_t) const = &Type::is_min;

    py::class_<Type>("Type")
        .def(py::init<halide_type_code_t, int, int>())
        .def("bytes", &Type::bytes)
        .def("code", &Type::code)
        .def("bits", &Type::bits)
        .def("lanes", &Type::lanes)
        .def("with_code", &Type::with_code)
        .def("with_bits", &Type::with_bits)
        .def("with_lanes", &Type::with_lanes)

        .def("is_bool", &Type::is_bool)
        .def("is_vector", &Type::is_vector)
        .def("is_scalar", &Type::is_scalar)
        .def("is_float", &Type::is_float)
        .def("is_int", &Type::is_int)
        .def("is_uint", &Type::is_uint)
        .def("is_handle", &Type::is_handle)
        .def("same_handle_type", &Type::same_handle_type)
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self < py::self)
        .def("element_of", &Type::element_of)
        .def("can_represent", can_represent_method)
        .def("is_max", is_max_i)
        .def("is_min", is_min_i)
        .def("max", &Type::max)
        .def("min", &Type::min)
        .def("__repr__", &type_repr)
        .def("__str__", &halide_type_to_string)
    ;

    py::def("Int", Int, (py::arg("bits"), py::arg("lanes") = 1));
    py::def("UInt", UInt, (py::arg("bits"), py::arg("lanes") = 1));
    py::def("Float", Float, (py::arg("bits"), py::arg("lanes") = 1));
    py::def("Bool", Bool, (py::arg("lanes") = 1));
    py::def("Handle", make_handle, (py::arg("lanes") = 1));

    py::enum_<halide_type_code_t>("TypeCode")
        .value("Int", Type::Int)
        .value("UInt", Type::UInt)
        .value("Float", Type::Float)
        .value("Handle", Type::Handle);
        // don't export_values(): we don't want the enums in the halide module
}

}  // namespace PythonBindings
}  // namespace Halide
