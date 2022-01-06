#include "PyPipeline.h"

#include <utility>

#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

namespace {

py::object realization_to_object(const Realization &r) {
    // Only one Buffer -> just return it
    if (r.size() == 1) {
        return py::cast(r[0]);
    }

    // Multiple -> return as Python tuple
    return to_python_tuple(r);
}

}  // namespace

void define_pipeline(py::module &m) {

    // Deliberately not supported, because they don't seem to make sense for Python:
    // - set_custom_allocator()
    // - set_custom_do_task()
    // - set_custom_do_par_for()
    // - set_jit_externs()
    // - get_jit_externs()
    // - jit_handlers()
    // - add_custom_lowering_pass()
    // - clear_custom_lowering_passes()
    // - custom_lowering_passes()
    // - add_autoscheduler()

    // Not supported yet, because we want to think about how to expose runtime
    // overrides in Python (https://github.com/halide/Halide/issues/2790):
    // - set_error_handler()
    // - set_custom_trace()
    // - set_custom_print()

    auto pipeline_class =
        py::class_<Pipeline>(m, "Pipeline")
            .def(py::init<>())
            .def(py::init<Func>())
            .def(py::init<const std::vector<Func> &>())

            .def("outputs", &Pipeline::outputs)

            .def("auto_schedule", (AutoSchedulerResults(Pipeline::*)(const std::string &, const Target &, const MachineParams &)) & Pipeline::auto_schedule,
                 py::arg("autoscheduler_name"), py::arg("target"), py::arg("machine_params") = MachineParams::generic())
            .def("auto_schedule", (AutoSchedulerResults(Pipeline::*)(const Target &, const MachineParams &)) & Pipeline::auto_schedule,
                 py::arg("target"), py::arg("machine_params") = MachineParams::generic())

            .def_static("set_default_autoscheduler_name", &Pipeline::set_default_autoscheduler_name,
                        py::arg("autoscheduler_name"))

            .def("get_func", &Pipeline::get_func,
                 py::arg("index"))
            .def("print_loop_nest", &Pipeline::print_loop_nest)

            .def("compile_to", &Pipeline::compile_to,
                 py::arg("outputs"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())

            .def("compile_to_bitcode", &Pipeline::compile_to_bitcode,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_llvm_assembly", &Pipeline::compile_to_llvm_assembly,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_object", &Pipeline::compile_to_object,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_header", &Pipeline::compile_to_header,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_assembly", &Pipeline::compile_to_assembly,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_c", &Pipeline::compile_to_c,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_file", &Pipeline::compile_to_file,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_static_library", &Pipeline::compile_to_static_library,
                 py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())

            .def("compile_to_lowered_stmt", &Pipeline::compile_to_lowered_stmt,
                 py::arg("filename"), py::arg("arguments"), py::arg("format") = StmtOutputFormat::Text, py::arg("target") = get_target_from_environment())

            .def("compile_to_multitarget_static_library", &Pipeline::compile_to_multitarget_static_library,
                 py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"))
            .def("compile_to_multitarget_object_files", &Pipeline::compile_to_multitarget_object_files,
                 py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"), py::arg("suffixes"))

            .def("compile_to_module", &Pipeline::compile_to_module,
                 py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment(), py::arg("linkage") = LinkageType::ExternalPlusMetadata)

            .def("compile_jit", &Pipeline::compile_jit, py::arg("target") = get_jit_target_from_environment())

            .def(
                "realize", [](Pipeline &p, Buffer<> buffer, const Target &target) -> void {
                    py::gil_scoped_release release;
                    p.realize(Realization(buffer), target);
                },
                py::arg("dst"), py::arg("target") = Target())

            // This will actually allow a list-of-buffers as well as a tuple-of-buffers, but that's OK.
            .def(
                "realize", [](Pipeline &p, std::vector<Buffer<>> buffers, const Target &t) -> void {
                    py::gil_scoped_release release;
                    p.realize(Realization(buffers), t);
                },
                py::arg("dst"), py::arg("target") = Target())

            .def(
                "realize", [](Pipeline &p, std::vector<int32_t> sizes, const Target &target) -> py::object {
                    std::optional<Realization> r;
                    {
                        py::gil_scoped_release release;
                        r = p.realize(std::move(sizes), target);
                    }
                    return realization_to_object(*r);
                },
                py::arg("sizes") = std::vector<int32_t>{}, py::arg("target") = Target())

            .def(
                "infer_input_bounds", [](Pipeline &p, const py::object &dst, const Target &target) -> void {
                    // dst could be Buffer<>, vector<Buffer>, or vector<int>
                    try {
                        Buffer<> b = dst.cast<Buffer<>>();
                        p.infer_input_bounds(b, target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<Buffer<>> v = dst.cast<std::vector<Buffer<>>>();
                        p.infer_input_bounds(Realization(v), target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<int32_t> v = dst.cast<std::vector<int32_t>>();
                        p.infer_input_bounds(v, target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    throw py::value_error("Invalid arguments to infer_input_bounds");
                },
                py::arg("dst"), py::arg("target") = get_jit_target_from_environment())

            .def("infer_arguments", [](Pipeline &p) -> std::vector<Argument> {
                return p.infer_arguments();
            })

            .def("defined", &Pipeline::defined)
            .def("invalidate_cache", &Pipeline::invalidate_cache)

            .def("__repr__", [](const Pipeline &p) -> std::string {
                std::ostringstream o;
                o << "<halide.Pipeline [";
                std::string comma;
                for (auto &f : p.outputs()) {
                    o << comma << "'" << f.name() << "'";
                    comma = ",";
                }
                o << "]>";
                return o.str();
            });
}

}  // namespace PythonBindings
}  // namespace Halide
