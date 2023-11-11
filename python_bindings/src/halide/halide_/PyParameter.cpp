#include "PyParameter.h"

#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

template<typename TYPE>
void add_scalar_methods(py::class_<Parameter> &parameter_class) {
    parameter_class
        .def("scalar", &Parameter::scalar<TYPE>)
        .def(
            "set_scalar", [](Parameter &parameter, TYPE value) -> void {
                parameter.set_scalar<TYPE>(value);
            },
            py::arg("value"));
}

}  // namespace

void define_parameter(py::module &m) {
    // Disambiguate some ambigious methods
    void (Parameter::*set_scalar_method)(const Type &t, halide_scalar_value_t val) = &Parameter::set_scalar;

    auto parameter_class =
        py::class_<Parameter>(m, "Parameter")
            .def(py::init<>())
            .def(py::init<const Parameter &>(), py::arg("p"))
            .def(py::init<const Type &, bool, int>())
            .def(py::init<const Type &, bool, int, const std::string &>())
            .def("_to_argument", [](const Parameter &p) -> Argument {
                return Argument(p.name(),
                                p.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                                p.type(),
                                p.dimensions(),
                                p.get_argument_estimates());
            })
            .def("__repr__", [](const Parameter &p) -> std::string {
                std::ostringstream o;
                o << "<halide.Parameter '" << p.name() << "'";
                if (!p.defined()) {
                    o << " (undefined)";
                } else {
                    // TODO: add dimensions to this
                    o << " type " << halide_type_to_string(p.type());
                }
                o << ">";
                return o.str();
            })
            .def("type", &Parameter::type)
            .def("dimensions", &Parameter::dimensions)
            .def("name", &Parameter::name)
            .def("is_buffer", &Parameter::is_buffer)
            .def("scalar_expr", &Parameter::scalar_expr)
            .def("set_scalar", set_scalar_method, py::arg("value_type"), py::arg("value"))
            .def("buffer", &Parameter::buffer)
            .def("set_buffer", &Parameter::set_buffer, py::arg("buffer"))
            .def("same_as", &Parameter::same_as, py::arg("other"))
            .def("defined", &Parameter::defined)
            .def("set_min_constraint", &Parameter::set_min_constraint, py::arg("dim"), py::arg("expr"))
            .def("set_extent_constraint", &Parameter::set_extent_constraint, py::arg("dim"), py::arg("expr"))
            .def("set_stride_constraint", &Parameter::set_stride_constraint, py::arg("dim"), py::arg("expr"))
            .def("set_min_constraint_estimate", &Parameter::set_min_constraint_estimate, py::arg("dim"), py::arg("expr"))
            .def("set_extent_constraint_estimate", &Parameter::set_extent_constraint_estimate, py::arg("dim"), py::arg("expr"))
            .def("set_host_alignment", &Parameter::set_host_alignment, py::arg("bytes"))
            .def("min_constraint", &Parameter::min_constraint, py::arg("dim"))
            .def("extent_constraint", &Parameter::extent_constraint, py::arg("dim"))
            .def("stride_constraint", &Parameter::stride_constraint, py::arg("dim"))
            .def("min_constraint_estimate", &Parameter::min_constraint_estimate, py::arg("dim"))
            .def("extent_constraint_estimate", &Parameter::extent_constraint_estimate, py::arg("dim"))
            .def("host_alignment", &Parameter::host_alignment)
            .def("buffer_constraints", &Parameter::buffer_constraints)
            .def("set_min_value", &Parameter::set_min_value, py::arg("expr"))
            .def("min_value", &Parameter::min_value)
            .def("set_max_value", &Parameter::set_max_value, py::arg("expr"))
            .def("max_value", &Parameter::max_value)
            .def("set_estimate", &Parameter::set_estimate, py::arg("expr"))
            .def("estimate", &Parameter::estimate)
            .def("set_default_value", &Parameter::set_default_value, py::arg("expr"))
            .def("default_value", &Parameter::default_value)
            .def("get_argument_estimates", &Parameter::get_argument_estimates)
            .def("store_in", &Parameter::store_in, py::arg("memory_type"))
            .def("memory_type", &Parameter::memory_type);

    add_scalar_methods<bool>(parameter_class);
    add_scalar_methods<uint8_t>(parameter_class);
    add_scalar_methods<uint16_t>(parameter_class);
    add_scalar_methods<uint32_t>(parameter_class);
    add_scalar_methods<uint64_t>(parameter_class);
    add_scalar_methods<int8_t>(parameter_class);
    add_scalar_methods<int16_t>(parameter_class);
    add_scalar_methods<int32_t>(parameter_class);
    add_scalar_methods<int64_t>(parameter_class);
    add_scalar_methods<float>(parameter_class);
    add_scalar_methods<double>(parameter_class);
}

}  // namespace PythonBindings
}  // namespace Halide
