#include "PyParam.h"

#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

namespace {

template<typename T>
void set_param_value(Param<> &param, T value, bool estimate = false) {
    if constexpr (std::is_same_v<T, double>) {
        // nanobind only considers float32 a non-converting match when exact.
        // Round Python floats for float32 Params, as the former binding did.
        if (param.type() == Float(32)) {
            set_param_value(param, static_cast<float>(value), estimate);
            return;
        }
    }
    if (estimate) {
        param.set_estimate<T>(value);
    } else {
        param.set<T>(value);
    }
}

template<typename TYPE>
void add_param_methods(py::class_<Param<>> &param_class) {
    param_class
        .def("__init__", [](Param<> *self, const Type &type, TYPE value) {
                 Param<> param(type);
                 set_param_value(param, value);
                 new (self) Param<>(param); }, py::arg("type"), py::arg("value"))
        .def("__init__", [](Param<> *self, const Type &type, const std::string &name, TYPE value) {
                 Param<> param(type, name);
                 set_param_value(param, value);
                 new (self) Param<>(param); }, py::arg("type"), py::arg("name"), py::arg("value"))
        .def("set", [](Param<> &param, TYPE value) -> void { set_param_value(param, value); },
             py::arg("value"))
        .def("set_estimate",  //
             [](Param<> &param, TYPE value) -> void {
                 set_param_value(param, value, true);  //
             },
             py::arg("value"));
}

}  // namespace

void define_param(py::module_ &m) {
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
    add_param_methods<double>(param_class);

    add_binary_operators(param_class);

    m.def("user_context_value", &user_context_value);
}

}  // namespace PythonBindings
}  // namespace Halide
