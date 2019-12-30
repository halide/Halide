#include "PyMachineParams.h"

namespace Halide {
namespace PythonBindings {

void define_machine_params(py::module &m) {
    auto machine_params_class = py::class_<MachineParams>(m, "MachineParams")
                                    .def(py::init<int32_t, int32_t, int32_t>(),
                                         py::arg("parallelism"), py::arg("last_level_cache_size"), py::arg("balance"))
                                    .def(py::init<std::string>())
                                    .def_readwrite("parallelism", &MachineParams::parallelism)
                                    .def_readwrite("last_level_cache_size", &MachineParams::last_level_cache_size)
                                    .def_readwrite("balance", &MachineParams::balance)
                                    .def_static("generic", &MachineParams::generic)
                                    .def("__str__", &MachineParams::to_string)
                                    .def("__repr__", [](const MachineParams &mp) -> std::string {
                                        std::ostringstream o;
                                        o << "<halide.MachineParams " << mp.to_string() << ">";
                                        return o.str();
                                    });
}

}  // namespace PythonBindings
}  // namespace Halide
