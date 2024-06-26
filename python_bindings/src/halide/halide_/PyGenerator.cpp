#include "PyGenerator.h"

#include <pybind11/embed.h>

namespace Halide {
namespace PythonBindings {

namespace {

using Halide::Internal::AbstractGenerator;
using Halide::Internal::AbstractGeneratorPtr;
using Halide::Internal::GeneratorFactoryProvider;
using ArgInfo = Halide::Internal::AbstractGenerator::ArgInfo;
using Halide::Parameter;
using Halide::Internal::ArgInfoDirection;
using Halide::Internal::ArgInfoKind;

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
    // The name declared in the Python function's decorator
    const std::string name_;

    // The instance of the Python class
    py::object generator_;

public:
    // Note: this ctor should not throw any exceptions. Success will be checked
    // by calling is_valid() later.
    explicit PyGeneratorBase(const GeneratorContext &context, const std::string &name)
        : name_(name),
          generator_(py::module_::import("halide").attr("_create_python_generator")(name, context)) {  // could be None!
    }

    bool is_valid() const {
        if (name_.empty() || generator_.is(py::none())) {
            return false;
        }
        return true;
    }

    std::string name() override {
        return name_;
    }

    GeneratorContext context() const override {
        return generator_.attr("context")().cast<GeneratorContext>();
    }

    std::vector<ArgInfo> arginfos() override {
        return args_to_vector<ArgInfo>(generator_.attr("_get_arginfos")());
    }

    bool allow_out_of_order_inputs_and_outputs() const override {
        return generator_.attr("allow_out_of_order_inputs_and_outputs")().cast<bool>();
    }

    void set_generatorparam_value(const std::string &name, const std::string &value) override {
        generator_.attr("_set_generatorparam_value")(name, value);
    }

    void set_generatorparam_value(const std::string &name, const LoopLevel &value) override {
        _halide_user_assert(false) << "Python Genrators should never see LoopLevels for GeneratorParam values.";
    }

    Pipeline build_pipeline() override {
        return generator_.attr("_build_pipeline")().cast<Pipeline>();
    }

    std::vector<Parameter> input_parameter(const std::string &name) override {
        return {generator_.attr("_get_input_parameter")(name).cast<Parameter>()};
    }

    std::vector<Func> output_func(const std::string &name) override {
        return {generator_.attr("_get_output_func")(name).cast<Func>()};
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

    bool emit_hlpipe(const std::string & /*hlpipe_file_path*/) override {
        // Python Generators don't support this yet ...
        // but don't throw an error, just return false.
        return false;
    }
};

class PyGeneratorFactoryProvider : public GeneratorFactoryProvider {
public:
    PyGeneratorFactoryProvider() = default;

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
            .def(py::init([](const std::string &name, ArgInfoDirection dir, ArgInfoKind kind, std::vector<Type> types, int dimensions) -> ArgInfo {
                     return ArgInfo{name, dir, kind, std::move(types), dimensions};
                 }),
                 py::arg("name"), py::arg("dir"), py::arg("kind"), py::arg("types"), py::arg("dimensions"))
            .def_readonly("name", &ArgInfo::name)
            .def_readonly("dir", &ArgInfo::dir)
            .def_readonly("kind", &ArgInfo::kind)
            .def_readonly("types", &ArgInfo::types)
            .def_readonly("dimensions", &ArgInfo::dimensions);

    // Note that we need py::dynamic_attr() here so that the Python code can add a token stack
    // for __enter__ and __exit__ handling
    auto generatorcontext_class =
        py::class_<GeneratorContext>(m, "GeneratorContext", py::dynamic_attr())
            .def(py::init<const Target &>(), py::arg("target"))
            .def(py::init<const Target &, const AutoschedulerParams &>(), py::arg("target"), py::arg("autoscheduler_params"))
            .def("target", &GeneratorContext::target)
            .def("autoscheduler_params", &GeneratorContext::autoscheduler_params)
            .def("__enter__", [](const GeneratorContext &context) -> py::object {
                auto _generatorcontext_enter = py::module_::import("halide").attr("_generatorcontext_enter");
                return _generatorcontext_enter(context);
            })
            .def("__exit__", [](const GeneratorContext &context, const py::object &exc_type, const py::object &exc_value, const py::object &exc_traceback) -> bool {
                auto _generatorcontext_exit = py::module_::import("halide").attr("_generatorcontext_exit");
                _generatorcontext_exit(context);
                return false;
            })
            .def("__repr__", [](const GeneratorContext &context) -> std::string {
                std::ostringstream o;
                o << "<halide.GeneratorContext " << context.target() << ">";
                return o.str();
            });

    m.def("main", []() -> void {
        py::object argv_object = py::module_::import("sys").attr("argv");
        std::vector<std::string> argv_vector = args_to_vector<std::string>(argv_object);
        std::vector<char *> argv;
        argv.reserve(argv_vector.size());
        for (auto &s : argv_vector) {
            argv.push_back(const_cast<char *>(s.c_str()));
        }
        int result = Halide::Internal::generate_filter_main((int)argv.size(), argv.data(), PyGeneratorFactoryProvider());
        if (result != 0) {
            // Some paths in generate_filter_main() will fail with user_error or similar (which throws an exception
            // due to how libHalide is built for python), but some paths just return an error code, so
            // be sure to handle both.
            throw std::runtime_error("Generator failed: " + std::to_string(result));
        }
    });

    m.def("_unique_name", []() -> std::string {
        return ::Halide::Internal::unique_name('p');
    });
}

}  // namespace PythonBindings
}  // namespace Halide
