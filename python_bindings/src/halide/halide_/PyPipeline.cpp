#include "PyPipeline.h"

#include <utility>

#include "PyError.h"
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

    py::class_<AutoschedulerParams>(m, "AutoschedulerParams")
        .def(py::init<>())
        .def(py::init<std::string>(), py::arg("name"))
        .def(py::init([](const std::string &name, const py::dict &extra) -> AutoschedulerParams {
                 // Manually convert the dict:
                 // we want to allow Python to pass in dicts that have non-string values for some keys;
                 // PyBind will reject these as a type failure. We'll stringify them here explicitly.
                 AutoschedulerParams asp(name);
                 for (auto item : extra) {
                     const std::string name = py::str(item.first).cast<std::string>();
                     const std::string value = py::str(item.second).cast<std::string>();
                     asp.extra[name] = value;
                 }
                 return asp;
             }),
             py::arg("target"), py::arg("autoscheduler_params"))
        .def_readwrite("name", &AutoschedulerParams::name)
        .def_readwrite("extra", &AutoschedulerParams::extra)
        .def("__repr__", [](const AutoSchedulerResults &o) -> std::string {
            return "<halide.AutoschedulerParams>";
        });

    auto pipeline_class =
        py::class_<Pipeline>(m, "Pipeline")
            .def(py::init<>())
            .def(py::init<Func>())
            .def(py::init<const std::vector<Func> &>())

            .def("outputs", &Pipeline::outputs)

            .def("apply_autoscheduler", (AutoSchedulerResults(Pipeline::*)(const Target &, const AutoschedulerParams &) const) & Pipeline::apply_autoscheduler,
                 py::arg("target"), py::arg("autoscheduler_params"))
            .def("get_func", &Pipeline::get_func,
                 py::arg("index"))
            .def("print_loop_nest", &Pipeline::print_loop_nest)

            .def(
                "compile_to", [](Pipeline &p, const std::map<OutputFileType, std::string> &output_files, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to(output_files, args, fn_name, to_aot_target(target));
                },
                py::arg("outputs"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target())

            .def(
                "compile_to_bitcode", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_bitcode(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target())
            .def(
                "compile_to_bitcode", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const Target &target) {
                    p.compile_to_bitcode(filename, args, "", to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("target") = Target())
            .def(
                "compile_to_llvm_assembly", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_llvm_assembly(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target())
            .def(
                "compile_to_llvm_assembly", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const Target &target) {
                    p.compile_to_llvm_assembly(filename, args, "", to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("target") = Target())
            .def(
                "compile_to_object", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_object(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target())
            .def(
                "compile_to_object", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const Target &target) {
                    p.compile_to_object(filename, args, "", to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("target") = Target())
            .def(
                "compile_to_header", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_header(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = Target())
            .def(
                "compile_to_assembly", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_assembly(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target())
            .def(
                "compile_to_assembly", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const Target &target) {
                    p.compile_to_assembly(filename, args, "", to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("target") = Target())
            .def(
                "compile_to_c", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_c(filename, args, fn_name, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = Target())
            .def(
                "compile_to_lowered_stmt", [](Pipeline &p, const std::string &filename, const std::vector<Argument> &args, StmtOutputFormat fmt, const Target &target) {
                    p.compile_to_lowered_stmt(filename, args, fmt, to_aot_target(target));
                },
                py::arg("filename"), py::arg("arguments"), py::arg("fmt") = Text, py::arg("target") = Target())
            .def(
                "compile_to_file", [](Pipeline &p, const std::string &filename_prefix, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_file(filename_prefix, args, fn_name, to_aot_target(target));
                },
                py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = Target())
            .def(
                "compile_to_static_library", [](Pipeline &p, const std::string &filename_prefix, const std::vector<Argument> &args, const std::string &fn_name, const Target &target) {
                    p.compile_to_static_library(filename_prefix, args, fn_name, to_aot_target(target));
                },
                py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = Target())

            .def("compile_to_multitarget_static_library", &Pipeline::compile_to_multitarget_static_library, py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"))
            .def("compile_to_multitarget_object_files", &Pipeline::compile_to_multitarget_object_files, py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"), py::arg("suffixes"))

            .def(
                "compile_to_module", [](Pipeline &p, const std::vector<Argument> &args, const std::string &fn_name, const Target &target, LinkageType linkage_type) -> Module {
                    return p.compile_to_module(args, fn_name, to_aot_target(target), linkage_type);
                },
                py::arg("arguments"), py::arg("fn_name"), py::arg("target") = Target(), py::arg("linkage") = LinkageType::ExternalPlusMetadata)

            .def(
                "compile_jit", [](Pipeline &p, const Target &target) {
                    p.compile_jit(to_jit_target(target));
                },
                py::arg("target") = Target())

            .def(
                "compile_to_callable", [](Pipeline &p, const std::vector<Argument> &args, const Target &target) {
                    return p.compile_to_callable(args, to_jit_target(target));
                },
                py::arg("arguments"), py::arg("target") = Target())

            .def(
                "realize", [](Pipeline &p, Buffer<> buffer, const Target &target) -> void {
                    py::gil_scoped_release release;

                    PyJITUserContext juc;
                    p.realize(&juc, Realization(std::move(buffer)), target);
                },
                py::arg("dst"), py::arg("target") = Target())

            // It's important to have this overload of realize() go first:
            // passing an empty list [] is ambiguous in Python, and could match to
            // either list-of-sizes or list-of-buffers... but the former is useful
            // (it allows realizing a 0-dimensional/scalar buffer) and the former is
            // not (it will always assert-fail). Putting this one first allows it to
            // be the first one chosen by the bindings in this case.
            .def(
                "realize", [](Pipeline &p, std::vector<int32_t> sizes, const Target &target) -> py::object {
                    std::optional<Realization> r;
                    {
                        py::gil_scoped_release release;

                        PyJITUserContext juc;
                        r = p.realize(&juc, std::move(sizes), target);
                    }
                    return realization_to_object(*r);
                },
                py::arg("sizes") = std::vector<int32_t>{}, py::arg("target") = Target())

            // This will actually allow a list-of-buffers as well as a tuple-of-buffers, but that's OK.
            .def(
                "realize", [](Pipeline &p, std::vector<Buffer<>> buffers, const Target &t) -> void {
                    py::gil_scoped_release release;

                    PyJITUserContext juc;
                    p.realize(&juc, Realization(std::move(buffers)), t);
                },
                py::arg("dst"), py::arg("target") = Target())

            .def(
                "infer_input_bounds", [](Pipeline &p, const py::object &dst, const Target &target) -> void {
                    const Target t = to_jit_target(target);
                    PyJITUserContext juc;

                    // dst could be Buffer<>, vector<Buffer>, or vector<int>
                    try {
                        Buffer<> b = dst.cast<Buffer<>>();
                        p.infer_input_bounds(&juc, b, t);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<Buffer<>> v = dst.cast<std::vector<Buffer<>>>();
                        p.infer_input_bounds(&juc, Realization(std::move(v)), t);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<int32_t> v = dst.cast<std::vector<int32_t>>();
                        p.infer_input_bounds(&juc, v, t);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    throw py::value_error("Invalid arguments to infer_input_bounds");
                },
                py::arg("dst"), py::arg("target") = Target())

            .def("infer_arguments", [](Pipeline &p) -> std::vector<Argument> {
                return p.infer_arguments();
            })

            .def("defined", &Pipeline::defined)
            .def("invalidate_cache", &Pipeline::invalidate_cache)

            .def(
                "add_requirement", [](Pipeline &p, const Expr &condition, const py::args &error_args) -> void {
                    auto v = collect_print_args(error_args);
                    p.add_requirement(condition, v);
                },
                py::arg("condition"))

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

    // TODO: These should really live in PyGenerator.cpp once that lands
    m.def(
        "create_callable_from_generator", [](const GeneratorContext &context, const std::string &name, const std::map<std::string, std::string> &generator_params) -> Callable {
            return create_callable_from_generator(context, name, generator_params);
        },
        py::arg("context"), py::arg("name"), py::arg("generator_params") = std::map<std::string, std::string>{});

    m.def(
        "create_callable_from_generator", [](const Target &target, const std::string &name, const std::map<std::string, std::string> &generator_params) -> Callable {
            return create_callable_from_generator(target, name, generator_params);
        },
        py::arg("target"), py::arg("name"), py::arg("generator_params") = std::map<std::string, std::string>{});
}

}  // namespace PythonBindings
}  // namespace Halide
