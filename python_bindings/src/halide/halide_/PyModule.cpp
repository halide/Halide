#include "PyModule.h"

namespace Halide {
namespace PythonBindings {

namespace {

}  // namespace

void define_module(py::module &m) {

    auto auto_scheduler_results_class =
        py::class_<AutoSchedulerResults>(m, "AutoSchedulerResults")
            .def(py::init<>())
            .def_readwrite("target", &AutoSchedulerResults::target)
            .def_readwrite("autoscheduler_params", &AutoSchedulerResults::autoscheduler_params)
            .def_readwrite("schedule_source", &AutoSchedulerResults::schedule_source)
            .def_readwrite("featurization", &AutoSchedulerResults::featurization)
            .def("__repr__", [](const AutoSchedulerResults &o) -> std::string {
                return "<halide.AutoSchedulerResults>";
            });

    auto module_class =
        py::class_<Module>(m, "Module")
            .def(py::init<const std::string &, const Target &>(), py::arg("name"), py::arg("target"))

            .def("target", &Module::target)
            .def("name", &Module::name)
            .def("get_auto_scheduler_results", &Module::get_auto_scheduler_results)
            .def("buffers", &Module::buffers)
            .def("submodules", &Module::submodules)

            .def("append", (void(Module::*)(const Buffer<> &)) & Module::append, py::arg("buffer"))
            .def("append", (void(Module::*)(const Module &)) & Module::append, py::arg("module"))

            .def("compile", &Module::compile, py::arg("outputs"))

            .def("compile_to_buffer", &Module::compile_to_buffer)

            .def("resolve_submodules", &Module::resolve_submodules)

            .def("remap_metadata_name", &Module::remap_metadata_name)
            .def("get_metadata_name_map", &Module::get_metadata_name_map)

            .def("set_auto_scheduler_results", &Module::set_auto_scheduler_results)

            // TODO: ExternalCode-related methods deliberately skipped for now.
            // .def("append", (void (Module::*)(const ExternalCode &)) &Module::append, py::arg("external_code"))
            // .def("external_code", &Module::external_code)

            // TODO: Internal::LoweredFunc-related methods deliberately skipped for now.
            // .def("functions", &Module::functions)
            // .def("get_function_by_name", &Module::get_function_by_name, py::arg("name"))
            // .def("append", (void (Module::*)(const Internal::LoweredFunc &)) &Module::append, py::arg("function"))

            .def("__repr__", [](const Module &m) -> std::string {
                std::ostringstream o;
                o << "<halide.Module '" << m.name() << "'>";
                return o.str();
            });

    m.def("link_modules", &link_modules, py::arg("name"), py::arg("modules"));
    m.def("compile_standalone_runtime", (void (*)(const std::string &, const Target &)) & compile_standalone_runtime, py::arg("filename"), py::arg("target"));
    using OutputMap = std::map<OutputFileType, std::string>;
    m.def("compile_standalone_runtime", (OutputMap(*)(const OutputMap &, const Target &)) & compile_standalone_runtime, py::arg("outputs"), py::arg("target"));

    // TODO: compile_multitarget() deliberately skipped for now.
}

}  // namespace PythonBindings
}  // namespace Halide
