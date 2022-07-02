#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
#include "PyMachineParams.h"

namespace Halide {
namespace PythonBindings {

void define_machine_params(py::module &m) {
    auto machine_params_class =
        py::class_<zMachineParams>(m, "zMachineParams")
            .def(py::init<int32_t, int32_t, int32_t>(),
                 py::arg("parallelism"), py::arg("last_level_cache_size"), py::arg("balance"))
            .def(py::init<std::string>())
            .def_readwrite("parallelism", &zMachineParams::parallelism)
            .def_readwrite("last_level_cache_size", &zMachineParams::last_level_cache_size)
            .def_readwrite("balance", &zMachineParams::balance)
            .def_static("generic", &zMachineParams::generic)
            .def("__str__", &zMachineParams::to_string)
            .def("__repr__", [](const zMachineParams &mp) -> std::string {
                std::ostringstream o;
                o << "<halide.zMachineParams " << mp.to_string() << ">";
                return o.str();
            });
}

}  // namespace PythonBindings
}  // namespace Halide
#endif
