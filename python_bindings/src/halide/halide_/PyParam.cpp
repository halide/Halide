#include "PyParam.h"

#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

template<typename TYPE>
void add_param_methods(py::class_<Param<>> &param_class) {
    param_class
        .def(py::init([](const Type &type, TYPE value) {
                 Param<> param(type);
                 param.set<TYPE>(value);
                 return param;
             }),
             py::arg("type"), py::arg("value"))
        .def(py::init([](const Type &type, const std::string &name, TYPE value) {
                 Param<> param(type, name);
                 param.set<TYPE>(value);
                 return param;
             }),
             py::arg("type"), py::arg("name"), py::arg("value"))
        .def(
            "set", [](Param<> &param, TYPE value) -> void {
                param.set<TYPE>(value);
            },
            py::arg("value"))
        .def(
            "set_estimate", [](Param<> &param, TYPE value) -> void {
                param.set_estimate<TYPE>(value);
            },
            py::arg("value"));
}

}  // namespace

void define_param(py::module &m) {
    auto param_class =
        py::class_<Param<>>(m, "Param")
            .def(py::init<Type>(), py::arg("type"))
            .def(py::init<Type, std::string>(), py::arg("type"), py::arg("name"))
            .def("name", &Param<>::name)
            .def("type", &Param<>::type)
            // The Param<> class is *always* defined (there isn't a way to
            // construct an instance without a Parameter attached).
            // .def("defined", &Param<>::defined)
            .def("set_range", &Param<>::set_range)
            .def("set_min_value", &Param<>::set_min_value)
            .def("set_max_value", &Param<>::set_max_value)
            .def("min_value", &Param<>::min_value)
            .def("max_value", &Param<>::max_value)
            .def("parameter", [](const Param<> &param) -> Parameter {
                return param.parameter();
            })

            .def("__repr__", [](const Param<> &param) -> std::string {
                std::ostringstream o;
                o << "<halide.Param '" << param.name() << "'"
                  << " type " << halide_type_to_string(param.type()) << ">";
                return o.str();
            });

    add_param_methods<bool>(param_class);
    add_param_methods<uint8_t>(param_class);
    add_param_methods<uint16_t>(param_class);
    add_param_methods<uint32_t>(param_class);
    add_param_methods<uint64_t>(param_class);
    add_param_methods<int8_t>(param_class);
    add_param_methods<int16_t>(param_class);
    add_param_methods<int32_t>(param_class);
    add_param_methods<int64_t>(param_class);
    add_param_methods<float>(param_class);
    add_param_methods<double>(param_class);

    add_binary_operators(param_class);

    m.def("user_context_value", &user_context_value);
}

}  // namespace PythonBindings
}  // namespace Halide
