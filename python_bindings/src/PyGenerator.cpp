#include "PyGenerator.h"

#include <pybind11/embed.h>

namespace Halide {
namespace PythonBindings {

namespace {

// Everything here is implicitly in module 'halide'
const char builtin_helpers_src[] = R"(
import inspect

_python_generator_functions = {}

def _get_function_argument_names(function):
    return inspect.signature(function).parameters.keys()

def _get_python_generator_function_names():
    return _python_generator_functions.keys()

def _find_python_generator_function(name):
    if not name in _python_generator_functions:
        return None
    return _python_generator_functions[name]["function"]

def _find_python_generator_arguments(name):
    if not name in _python_generator_functions:
        return None
    return _python_generator_functions[name]["arguments"]

def generator(name, arguments):
    def real_decorator(function):
        _python_generator_functions[name] = { "function": function, "arguments": arguments };
        return function
    return real_decorator

)";

using Halide::Internal::AbstractGenerator;
using Halide::Internal::AbstractGeneratorPtr;
using Halide::Internal::ExternsMap;
using Halide::Internal::GeneratorsForMain;
using Halide::Internal::IOKind;
using Halide::Internal::Parameter;

class PyGeneratorBase : public AbstractGenerator {
    // Boilerplate
    const GeneratorContext context_;

    // The name declared in the Python function's decorator
    const std::string name_;

    // The function
    py::object function_;

    // The Arguments declared in the Python function's decorator
    std::vector<Argument> arguments_;

    // Constants (aka GeneratorParams) TODO
    std::map<std::string, std::string> constants_;

    // State we build up
    std::map<std::string, py::object> input_objects_;  // Param<void> or ImageParam
    std::map<std::string, Parameter> input_parameters_;
    std::map<std::string, Func> output_funcs_;
    // Our Pipeline
    Pipeline pipeline_;

    std::vector<AbstractGenerator::ArgInfo> get_arginfos(bool inputs) {
        std::vector<AbstractGenerator::ArgInfo> arginfo;
        for (const auto &arg : arguments_) {
            if (inputs != arg.is_input()) {
                continue;
            }
            arginfo.push_back({arg.name,
                               arg.is_scalar() ? IOKind::Scalar : IOKind::Buffer,
                               std::vector<Type>{arg.type},
                               arg.is_scalar() ? 0 : arg.dimensions});
        }
        return arginfo;
    }

public:
    explicit PyGeneratorBase(const GeneratorContext &context, const std::string name)
        : context_{context.get_target(),
                   context.get_auto_schedule(),
                   context.get_machine_params()},
          name_(name) {
        auto m = py::module_::import("halide");
        // Don't throw exceptions; allow is_valid() to be called after creation instead.
        function_ = m.attr("_find_python_generator_function")(name);  // could be None!
        auto arguments_obj = m.attr("_find_python_generator_arguments")(name);
        if (!arguments_obj.is(py::none())) {
            arguments_ = args_to_vector<Argument>(arguments_obj);
        }
    }

    bool is_valid() const {
        if (name_.empty() || function_.is(py::none()) || arguments_.empty()) {
            return false;
        }
        return true;
    }

    std::string get_name() override {
        return name_;
    }

    GeneratorContext context() const override {
        return context_;
    }

    std::vector<ArgInfo> get_input_arginfos() override {
        return get_arginfos(true);
    }

    std::vector<ArgInfo> get_output_arginfos() override {
        return get_arginfos(false);
    }

    std::vector<std::string> get_generatorparam_names() override {
        std::vector<std::string> v;
        for (const auto &c : constants_) {
            v.push_back(c.first);
        }
        return v;
    }

    void set_generatorparam_value(const std::string &name, const std::string &value) override {
        // TODO: convert _halide_user_assert() into something else
        _halide_user_assert(!pipeline_.defined());
        _halide_user_assert(constants_.count(name) == 1) << "Unknown Constant: " << name;
        constants_[name] = value;
    }

    void set_generatorparam_value(const std::string &name, const LoopLevel &value) override {
        _halide_user_assert(!pipeline_.defined());
        _halide_user_assert(constants_.count(name) == 1) << "Unknown Constant: " << name;
        _halide_user_assert(false) << "This Generator has no LoopLevel constants.";
    }

    Pipeline build_pipeline() override {
        _halide_user_assert(!pipeline_.defined());

        // Validate the input argument specifications.
        size_t expected_inputs = 0, expected_outputs = 0;
        {
            auto m = py::module_::import("halide");
            auto input_names_obj = m.attr("_get_function_argument_names")(function_);
            auto input_names = args_to_vector<std::string>(input_names_obj);
            _halide_user_assert(input_names.at(0) == "context") << "The first argument to Generator " << name_ << " must be 'context'.";
            for (const auto &arg : arguments_) {
                if (arg.is_input()) {
                    _halide_user_assert(expected_outputs == 0) << "Generator " << name_ << " must list all Inputs in Arguments before listing any Outputs.";
                    expected_inputs++;
                } else {
                    expected_outputs++;
                }
            }
            _halide_user_assert(expected_inputs + 1 == input_names.size()) << "Generator " << name_ << " does not have the correct number of Inputs in its Argument list.";
            for (size_t i = 0; i < expected_inputs; i++) {
                _halide_user_assert(arguments_[i].name == input_names[i + 1]) << "Generator " << name_ << " has a name mismatch.";
            }
            _halide_user_assert(expected_outputs > 0) << "Generator " << name_ << " must declare at least one Output in Arguments.";
        }

        input_objects_["context"] = py::cast(context_);
        for (const auto &arg : arguments_) {
            if (!arg.is_input()) {
                continue;
            }
            if (arg.is_scalar()) {
                Param<void> param(arg.type, arg.name);
                input_objects_[arg.name] = py::cast(param);
                input_parameters_[arg.name] = param.parameter();
            } else {
                ImageParam param(arg.type, arg.dimensions, arg.name);
                input_objects_[arg.name] = py::cast(param);
                input_parameters_[arg.name] = param.parameter();
            }
        }

        py::dict kwargs = py::cast(input_objects_);
        py::object return_value = function_(**kwargs);
        if (py::isinstance<Func>(return_value)) {
            pipeline_ = Pipeline(return_value.cast<Func>());
        } else if (py::isinstance<py::list>(return_value)) {
            pipeline_ = Pipeline(args_to_vector<Func>(return_value));
        } else if (py::isinstance<py::tuple>(return_value)) {
            pipeline_ = Pipeline(args_to_vector<Func>(return_value));
        } else {
            pipeline_ = return_value.cast<Pipeline>();
        }

        // Validate the output argument specifications.
        {
            auto pipeline_outputs = pipeline_.outputs();
            _halide_user_assert(expected_outputs == pipeline_outputs.size()) << "Generator " << name_ << " does not return the correct number of Outputs.";
            size_t i = 0;
            for (const auto &arg : arguments_) {
                if (arg.is_input()) {
                    continue;
                }
                output_funcs_[arg.name] = pipeline_outputs.at(i++);
            }
        }

        return pipeline_;
    }

    std::vector<Parameter> get_parameters_for_input(const std::string &name) override {
        _halide_user_assert(pipeline_.defined());
        auto it = input_parameters_.find(name);
        _halide_user_assert(it != input_parameters_.end()) << "Unknown input: " << name;
        return {it->second};
    }

    std::vector<Func> get_funcs_for_output(const std::string &name) override {
        _halide_user_assert(pipeline_.defined());
        auto it = output_funcs_.find(name);
        _halide_user_assert(it != output_funcs_.end()) << "Unknown output: " << name;
        return {it->second};
    }

    ExternsMap get_external_code_map() override {
        // Python Generators don't support this (yet? ever?),
        // but don't throw an error, just return an empty map.
        return {};
    }

    void bind_input(const std::string &name, const std::vector<Parameter> &v) override {
        _halide_user_assert(false) << "Python Generators don't support bind_input()";
    }

    void bind_input(const std::string &name, const std::vector<Func> &v) override {
        _halide_user_assert(false) << "Python Generators don't support bind_input()";
    }

    void bind_input(const std::string &name, const std::vector<Expr> &v) override {
        _halide_user_assert(false) << "Python Generators don't support bind_input()";
    }

    bool emit_cpp_stub(const std::string & /*stub_file_path*/) override {
        // Python Generators don't support this (probably ever),
        // but don't throw an error, just return false.
        return false;
    }
};

class PyGeneratorsForMain : public GeneratorsForMain {
public:
    PyGeneratorsForMain() = default;

    std::vector<std::string> enumerate() const override {
        py::object f = py::module_::import("halide").attr("_get_python_generator_function_names");
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
    py::object scope = m.attr("__dict__");
    py::exec(builtin_helpers_src, scope);

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
