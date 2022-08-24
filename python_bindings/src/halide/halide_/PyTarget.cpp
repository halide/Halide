#include "PyTarget.h"

namespace Halide {
namespace PythonBindings {

namespace {

std::string target_repr(const Target &t) {
    std::ostringstream o;
    o << "<halide.Target " << t.to_string() << ">";
    return o.str();
}

}  // namespace

void define_target(py::module &m) {
    // Disambiguate some ambigious methods
    int (Target::*natural_vector_size_method)(const Type &t) const = &Target::natural_vector_size;
    bool (Target::*supports_type1_method)(const Type &t) const = &Target::supports_type;
    bool (Target::*supports_type2_method)(const Type &t, DeviceAPI device) const = &Target::supports_type;

    auto target_class =
        py::class_<Target>(m, "Target")
            .def(py::init<>())
            .def(py::init<const std::string &>())
            .def(py::init<Target::OS, Target::Arch, int>())
            .def(py::init<Target::OS, Target::Arch, int, Target::Processor>())
            .def(py::init<Target::OS, Target::Arch, int, std::vector<Target::Feature>>())
            .def(py::init<Target::OS, Target::Arch, int, Target::Processor, std::vector<Target::Feature>>())

            .def("__eq__", [](const Target &value, Target *value2) { return value2 && value == *value2; })
            .def("__ne__", [](const Target &value, Target *value2) { return !value2 || value != *value2; })

            .def_readwrite("os", &Target::os)
            .def_readwrite("arch", &Target::arch)
            .def_readwrite("bits", &Target::bits)
            .def_readwrite("processor_tune", &Target::processor_tune)

            .def("__repr__", &target_repr)
            .def("__str__", &Target::to_string)
            .def("to_string", &Target::to_string)

            .def("has_feature", (bool(Target::*)(Target::Feature) const) & Target::has_feature)
            .def("features_any_of", &Target::features_any_of, py::arg("features"))
            .def("features_all_of", &Target::features_all_of, py::arg("features"))

            .def("set_feature", &Target::set_feature, py::arg("f"), py::arg("value") = true)
            .def("set_features", &Target::set_features, py::arg("features"), py::arg("value") = true)
            .def("with_feature", &Target::with_feature, py::arg("feature"))
            .def("without_feature", &Target::without_feature, py::arg("feature"))
            .def("has_gpu_feature", &Target::has_gpu_feature)
            .def("supports_type", supports_type1_method, py::arg("type"))
            .def("supports_type", supports_type2_method, py::arg("type"), py::arg("device"))
            .def("supports_device_api", &Target::supports_device_api, py::arg("device"))
            .def("natural_vector_size", natural_vector_size_method, py::arg("type"))
            .def("has_large_buffers", &Target::has_large_buffers)
            .def("maximum_buffer_size", &Target::maximum_buffer_size)
            .def("supported", &Target::supported)
            .def_static("validate_target_string", &Target::validate_target_string, py::arg("name"));
    ;

    m.def("get_host_target", &get_host_target);
    m.def("get_target_from_environment", &get_target_from_environment);
    m.def("get_jit_target_from_environment", &get_jit_target_from_environment);
    m.def("target_feature_for_device_api", &target_feature_for_device_api);

    // TODO: this really belong in PyDeviceInterface.cpp (once it exists);
    // it's here as an expedient to make our tutorials work more cleanly.
    m.def("host_supports_target_device", &host_supports_target_device);
}

}  // namespace PythonBindings
}  // namespace Halide
