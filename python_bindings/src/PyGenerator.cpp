#include "PyGenerator.h"

#include <pybind11/embed.h>

extern "C" unsigned char builtin_helpers_src[];
extern "C" int builtin_helpers_src_length;

// Temporary, for development: slurp the Python source directly from disk.
// Don't check in with this defined. Duh.
#define LOAD_PY_FROM_FILE 1

namespace Halide {
namespace PythonBindings {

namespace {

using Halide::Internal::AbstractGenerator;
using Halide::Internal::AbstractGeneratorPtr;
using Halide::Internal::ExternsMap;
using Halide::Internal::GeneratorsForMain;
using ArgInfo = Halide::Internal::AbstractGenerator::ArgInfo;
using Halide::Internal::ArgInfoDir;
using Halide::Internal::ArgInfoKind;
using Halide::Internal::Parameter;

template<typename T>
std::map<std::string, T> dict_to_map(const py::dict &dict) {
    _halide_user_assert(!dict.is(py::none()));
    std::map<std::string, T> m;
    for (auto it : dict) {
        m[it.first.cast<std::string>()] = it.second.cast<T>();
    }
    return m;
}

class PyGeneratorBase : public AbstractGenerator {
    // Boilerplate
    const GeneratorContext context_;

    // The name declared in the Python function's decorator
    const std::string name_;

    // The Python class
    const py::object class_;

    // The instance of the Python class
    py::object generator_;

public:
    // Note: this ctor should not throw any exceptions. Success will be checked
    // by calling is_valid() later.
    explicit PyGeneratorBase(const GeneratorContext &context, const std::string name)
        : context_(context),
          name_(name),
          class_(py::module_::import("halide").attr("_find_python_generator")(name)),  // could be None!
          generator_(class_.is(py::none()) ? py::none() : class_(context_)) {          // could be None!
    }

    bool is_valid() const {
        if (name_.empty() || class_.is(py::none()) || generator_.is(py::none())) {
            return false;
        }
        return true;
    }

    std::string get_name() override {
        return name_;
    }

    GeneratorContext get_context() const override {
        return context_;
    }

    std::vector<ArgInfo> get_arginfos() override {
        return args_to_vector<ArgInfo>(generator_.attr("_get_arginfos")());
    }

    void set_generatorparam_value(const std::string &name, const std::string &value) override {
        generator_.attr("_set_generatorparam_value")(name, value);
    }

    void set_generatorparam_value(const std::string &name, const LoopLevel &value) override {
        generator_.attr("_set_generatorparam_value")(name, value);
    }

    Pipeline build_pipeline() override {
        return generator_.attr("_build_pipeline")().cast<Pipeline>();
    }

    std::vector<Parameter> get_parameters_for_input(const std::string &name) override {
        return args_to_vector<Parameter>(generator_.attr("_get_parameters_for_input")(name));
    }

    std::vector<Func> get_funcs_for_output(const std::string &name) override {
        return args_to_vector<Func>(generator_.attr("_get_funcs_for_output")(name));
    }

    ExternsMap get_external_code_map() override {
        // Python Generators don't support this (yet? ever?),
        // but don't throw an error, just return an empty map.
        return {};
    }

    void bind_input(const std::string &name, const std::vector<Parameter> &v) override {
        generator_.attr("_bind_input")(v);
    }

    void bind_input(const std::string &name, const std::vector<Func> &v) override {
        generator_.attr("_bind_input")(v);
    }

    void bind_input(const std::string &name, const std::vector<Expr> &v) override {
        generator_.attr("_bind_input")(v);
    }

    bool emit_cpp_stub(const std::string & /*stub_file_path*/) override {
        // Python Generators don't support this (and *never* will, so don't ask),
        // but don't throw an error, just return false.
        return false;
    }
};

class PyGeneratorsForMain : public GeneratorsForMain {
public:
    PyGeneratorsForMain() = default;

    std::vector<std::string> enumerate() const override {
        py::object f = py::module_::import("halide").attr("_get_python_generator_names");
        return args_to_vector<std::string>(f());
    }
    AbstractGeneratorPtr create(const std::string &name,
                                const Halide::GeneratorContext &context) const override {
        auto g = std::make_unique<PyGeneratorBase>(context, name);
        if (!g->is_valid()) {
            return nullptr;
        }
        return g;
    }
};

}  // namespace

void define_generator(py::module &m) {
    auto arginfo_class =
        py::class_<ArgInfo>(m, "ArgInfo")
            .def(py::init<>())
            .def(py::init([](const std::string &name, ArgInfoDir dir, ArgInfoKind kind, std::vector<Type> types, int dimensions) -> ArgInfo {
                     return ArgInfo{name, dir, kind, types, dimensions};
                 }),
                 py::arg("name"), py::arg("dir"), py::arg("kind"), py::arg("types"), py::arg("dimensions"))
            .def_readonly("name", &ArgInfo::name)
            .def_readonly("dir", &ArgInfo::dir)
            .def_readonly("kind", &ArgInfo::kind)
            .def_readonly("types", &ArgInfo::types)
            .def_readonly("dimensions", &ArgInfo::dimensions);

    auto generatorcontext_class =
        py::class_<GeneratorContext>(m, "GeneratorContext")
            .def(py::init<const Target &, bool, const MachineParams &>(),
                 py::arg("target"), py::arg("auto_schedule") = false, py::arg("machine_params") = MachineParams::generic())
            .def("get_target", &GeneratorContext::get_target)
            .def("get_auto_schedule", &GeneratorContext::get_auto_schedule)
            .def("get_machine_params", &GeneratorContext::get_machine_params)
            // TODO: handle get_externs_map() someday?
            .def("__repr__", [](const GeneratorContext &context) -> std::string {
                std::ostringstream o;
                o << "<halide.GeneratorContext " << context.get_target() << ">";
                return o.str();
            });

    py::object scope = m.attr("__dict__");
#if LOAD_PY_FROM_FILE
    #pragma message "WARNING, compiling with LOAD_PY_FROM_FILE enabled"
    std::string src = Internal::get_env_variable("HL_DEV_PATH_TO_PYTHON_SRC");
    _halide_user_assert(!src.empty()) << "You must define HL_DEV_PATH_TO_PYTHON_SRC as the absolute path to builtin_helpers_src.py";
    py::eval_file(src, scope);
#else
    py::exec(py::str((const char *)builtin_helpers_src, builtin_helpers_src_length), scope);
#endif

    m.def("main", []() -> void {
        py::object argv_object = py::module_::import("sys").attr("argv");
        std::vector<std::string> argv_vector = args_to_vector<std::string>(argv_object);
        std::vector<char *> argv;
        argv.reserve(argv_vector.size());
        for (auto &s : argv_vector) {
            argv.push_back(const_cast<char *>(s.c_str()));
        }
        std::ostringstream error_stream;
        int result = Halide::Internal::generate_filter_main((int)argv.size(), argv.data(), error_stream, PyGeneratorsForMain());
        if (!error_stream.str().empty()) {
            py::print(error_stream.str(), py::arg("end") = "");
        }
        if (result != 0) {
            // Some paths in generate_filter_main() will fail with user_error or similar (which throws an exception
            // due to how libHalide is built for python), but some paths just return an error code, so
            // be sure to handle both.
            throw std::runtime_error("Generator failed: " + std::to_string(result));
        }
    });
}

}  // namespace PythonBindings
}  // namespace Halide
