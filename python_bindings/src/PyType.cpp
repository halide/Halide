#include "PyType.h"

#include <boost/format.hpp>
#include <boost/python.hpp>
#include <string>
#include <vector>

#include "Halide.h"

using Halide::Bool;
using Halide::Float;
using Halide::Handle;
using Halide::Int;
using Halide::Type;
using Halide::UInt;

namespace {

Halide::Type make_handle(int lanes) {
    return Handle(lanes, nullptr);
}

std::string type_repr(const Type &t) {
    return boost::str(boost::format("<halide.Type %s>") % halide_type_to_string(t));
}

}  // namespace

std::string halide_type_to_string(const Halide::Type &type) {
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
    using Halide::Type;
    namespace p = boost::python;

    bool (Type::*can_represent_method)(Type) const = &Type::can_represent;
    // Python doesn't have unsigned integers -- all integers are signed --
    // so we'll never see anything that can usefully be routed to the uint64_t
    // overloads of these methods.
    bool (Type::*is_max_i)(int64_t) const = &Type::is_max;
    bool (Type::*is_min_i)(int64_t) const = &Type::is_min;

    boost::python::class_<Type>("Type")
        .def(boost::python::init<halide_type_code_t, int, int>())
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
        .def(boost::python::self == boost::python::self)
        .def(boost::python::self != boost::python::self)
        .def(boost::python::self < boost::python::self)
        .def("element_of", &Type::element_of)
        .def("can_represent", can_represent_method)
        .def("is_max", is_max_i)
        .def("is_min", is_min_i)
        .def("max", &Type::max)
        .def("min", &Type::min)
        .def("__repr__", &type_repr)
        .def("__str__", &halide_type_to_string)
    ;

    boost::python::def("Int", Int, (boost::python::arg("bits"), boost::python::arg("lanes") = 1));
    boost::python::def("UInt", UInt, (boost::python::arg("bits"), boost::python::arg("lanes") = 1));
    boost::python::def("Float", Float, (boost::python::arg("bits"), boost::python::arg("lanes") = 1));
    boost::python::def("Bool", Bool, (boost::python::arg("lanes") = 1));
    boost::python::def("Handle", make_handle, (boost::python::arg("lanes") = 1));

    boost::python::enum_<halide_type_code_t>("TypeCode")
        .value("Int", Type::Int)
        .value("UInt", Type::UInt)
        .value("Float", Type::Float)
        .value("Handle", Type::Handle);
        // don't export_values(): we don't want the enums in the halide module
}
