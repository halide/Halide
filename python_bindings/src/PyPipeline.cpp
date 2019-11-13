#include "PyPipeline.h"

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

    auto pipeline_class = py::class_<Pipeline>(m, "Pipeline")
        .def(py::init<>())
        .def(py::init<Func>())
        .def(py::init<const std::vector<Func> &>())

        .def("outputs", &Pipeline::outputs)

        .def("auto_schedule", (AutoSchedulerResults (Pipeline::*)(const std::string &, const Target &, const MachineParams &)) &Pipeline::auto_schedule,
            py::arg("autoscheduler_name"), py::arg("target"), py::arg("machine_params") = MachineParams::generic())
        .def("auto_schedule", (AutoSchedulerResults (Pipeline::*)(const Target &, const MachineParams &)) &Pipeline::auto_schedule,
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
            py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets") = get_target_from_environment())

        .def("compile_to_module", &Pipeline::compile_to_module,
            py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment(), py::arg("linkage") = LinkageType::ExternalPlusMetadata)

        .def("compile_jit", &Pipeline::compile_jit, py::arg("target") = get_jit_target_from_environment())

        .def("realize", [](Pipeline &p, Buffer<> buffer, const Target &target, const ParamMap &param_map) -> void {
            p.realize(Realization(buffer), target);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // This will actually allow a list-of-buffers as well as a tuple-of-buffers, but that's OK.
        .def("realize", [](Pipeline &p, std::vector<Buffer<>> buffers, const Target &t, const ParamMap &param_map) -> void {
            p.realize(Realization(buffers), t);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Pipeline &p, std::vector<int32_t> sizes, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(p.realize(sizes, target, param_map));
        }, py::arg("sizes") = std::vector<int32_t>{}, py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // TODO: deprecate in favor of std::vector<int32_t> size version?
        .def("realize", [](Pipeline &p, int x_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(p.realize(x_size, target, param_map));
        }, py::arg("x_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // TODO: deprecate in favor of std::vector<int32_t> size version?
        .def("realize", [](Pipeline &p, int x_size, int y_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(p.realize(x_size, y_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // TODO: deprecate in favor of std::vector<int32_t> size version?
        .def("realize", [](Pipeline &p, int x_size, int y_size, int z_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(p.realize(x_size, y_size, z_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // TODO: deprecate in favor of std::vector<int32_t> size version?
        .def("realize", [](Pipeline &p, int x_size, int y_size, int z_size, int w_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(p.realize(x_size, y_size, z_size, w_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("w_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("infer_input_bounds", [](Pipeline &p, int x_size, int y_size, int z_size, int w_size, const ParamMap &param_map) -> void {
            p.infer_input_bounds(x_size, y_size, z_size, w_size, param_map);
        }, py::arg("x_size") = 0, py::arg("y_size") = 0, py::arg("z_size") = 0, py::arg("w_size") = 0, py::arg("param_map") = ParamMap())

        .def("infer_input_bounds", [](Pipeline &p, Buffer<> buffer, const ParamMap &param_map) -> void {
            p.infer_input_bounds(Realization(buffer), param_map);
        }, py::arg("dst"), py::arg("param_map") = ParamMap())
        .def("infer_input_bounds", [](Pipeline &p, std::vector<Buffer<>> buffers, const ParamMap &param_map) -> void {
            p.infer_input_bounds(Realization(buffers), param_map);
        }, py::arg("dst"), py::arg("param_map") = ParamMap())

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
        })
    ;
}

}  // namespace PythonBindings
}  // namespace Halide
