// Note that this deliberately does *not* include PyHalide.h,
// or depend on any of the code in src: this is intended to be
// a minimal, generic wrapper to expose an arbitrary Generator
// for stub usage in Python.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>

#include <vector>

#include "Halide.h"

static_assert(PYBIND11_VERSION_MAJOR == 2 && PYBIND11_VERSION_MINOR >= 6,
              "Halide requires PyBind 2.6+");

static_assert(PY_VERSION_HEX >= 0x03000000,
              "We appear to be compiling against Python 2.x rather than 3.x, which is not supported.");

namespace py = pybind11;

namespace Halide {
namespace PythonBindings {

using Parameter = Internal::Parameter;
using ArgInfoKind = Internal::ArgInfoKind;
using ArgInfo = Internal::AbstractGenerator::ArgInfo;
using GeneratorFactory = Internal::GeneratorFactory;
using StubInput = Internal::StubInput;
using StubInputBuffer = Internal::StubInputBuffer<void>;

namespace {

// This seems redundant to the code in PyError.cpp, but is necessary
// in case the Stub builder links in a separate copy of libHalide, rather
// sharing the same halide.so that is built by default.
void halide_python_error(JITUserContext *, const char *msg) {
    throw Error(msg);
}

void halide_python_print(JITUserContext *, const char *msg) {
    py::gil_scoped_acquire acquire;
    py::print(msg, py::arg("end") = "");
}

class HalidePythonCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char *msg) override {
        py::gil_scoped_acquire acquire;
        py::print(msg, py::arg("end") = "");
    }

    void error(const char *msg) override {
        throw Error(msg);
        // This method must not return!
    }
};

void install_error_handlers(py::module &m) {
    static HalidePythonCompileTimeErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    Halide::JITHandlers handlers;
    handlers.custom_error = halide_python_error;
    handlers.custom_print = halide_python_print;
    Halide::Internal::JITSharedRuntime::set_default_handlers(handlers);

    static py::object halide_error = py::module_::import("halide").attr("HalideError");
    if (halide_error.is(py::none())) {
        throw std::runtime_error("Could not find halide.HalideError");
    }

    py::register_exception_translator([](std::exception_ptr p) {  // NOLINT
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const Error &e) {
            PyErr_SetString(halide_error.ptr(), e.what());
        }
    });
}

// Anything that defines __getitem__ looks sequencelike to pybind,
// so also check for __len_ to avoid things like Buffer and Func here.
bool is_real_sequence(const py::object &o) {
    return py::isinstance<py::sequence>(o) && py::hasattr(o, "__len__");
}

template<typename T>
struct cast_error_string {
    std::string operator()(const py::handle &h, const std::string &name) {
        return "Unable to cast Input " + name + " to " + py::type_id<T>() + " from " + (std::string)py::str(py::type::handle_of(h));
    }
};

template<>
std::string cast_error_string<Buffer<>>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires an ImageParam or Buffer argument when using generate(), but saw " << (std::string)py::str(py::type::handle_of(h));
    return o.str();
}

template<>
std::string cast_error_string<Func>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires a Func argument when using generate(), but saw " << (std::string)py::str(py::type::handle_of(h));
    return o.str();
}

template<>
std::string cast_error_string<Expr>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires a Param (or scalar literal) argument when using generate(), but saw " << (std::string)py::str(py::type::handle_of(h));
    return o.str();
}

template<typename T>
T cast_to(const py::handle &h, const std::string &name) {
    // We want to ensure that the error thrown is one that will be translated
    // to `hl.Error` in Python.
    try {
        return h.cast<T>();
    } catch (const std::exception &e) {
        throw Halide::Error(cast_error_string<T>()(h, name));
    }
}

template<>
Parameter cast_to(const py::handle &h, const std::string &name) {
    auto b = cast_to<Buffer<>>(h, name);
    Parameter p(b.type(), true, b.dimensions());
    p.set_buffer(b);
    return p;
}

template<typename T>
std::vector<T> to_input_vector(const py::object &value, const std::string &name) {
    std::vector<T> v;
    if (is_real_sequence(value)) {
        for (const auto &o : py::reinterpret_borrow<py::sequence>(value)) {
            v.push_back(cast_to<T>(o, name));
        }
    } else {
        v.push_back(cast_to<T>(value, name));
    }
    return v;
}

py::object generate_impl(const GeneratorFactory &factory,
                         const GeneratorContext &context,
                         const py::args &args,
                         const py::kwargs &kwargs) {
    auto generator = factory(context);

    const auto arg_infos = generator->arginfos();
    std::vector<ArgInfo> input_arguments, output_arguments;
    std::map<std::string, ArgInfo> input_arguments_map;
    std::set<std::string> inputs_seen;
    for (const auto &a : arg_infos) {
        if (a.dir == Internal::ArgInfoDirection::Input) {
            input_arguments.push_back(a);
            input_arguments_map[a.name] = a;
        } else {
            output_arguments.push_back(a);
        }
    }
    size_t kw_inputs_specified = 0;

    // GeneratorParams are always specified as an optional named parameter
    // called "generator_params", which is expected to be a python dict.
    // If generatorparams are specified, do them first, before any Inputs.
    if (kwargs.contains("generator_params")) {
        py::dict gp = py::cast<py::dict>(kwargs["generator_params"]);
        for (auto item : gp) {
            const std::string gp_name = py::str(item.first).cast<std::string>();
            const py::handle gp_value = item.second;
            if (py::isinstance<LoopLevel>(gp_value)) {
                generator->set_generatorparam_value(gp_name, gp_value.cast<LoopLevel>());
            } else if (py::isinstance<py::list>(gp_value)) {
                // Convert [hl.UInt(8), hl.Int(16)] -> uint8,int16
                std::string v;
                for (auto t : gp_value) {
                    if (!v.empty()) {
                        v += ",";
                    }
                    v += py::str(t).cast<std::string>();
                }
                generator->set_generatorparam_value(gp_name, v);
            } else {
                generator->set_generatorparam_value(gp_name, py::str(gp_value).cast<std::string>());
            }
        }
    }

    // Inputs can be specified by either positional or named args,
    // but may not be mixed. (i.e., if any inputs are specified as a named
    // argument, they all must be specified that way; otherwise they must all be
    // positional, in the order declared in the Generator.)

    const auto bind_one = [&generator](py::handle h, const ArgInfo &a) {
        py::object o = py::cast<py::object>(h);
        if (a.kind == ArgInfoKind::Buffer) {
            generator->bind_input(a.name, to_input_vector<Parameter>(o, a.name));
        } else if (a.kind == ArgInfoKind::Function) {
            generator->bind_input(a.name, to_input_vector<Func>(o, a.name));
        } else {
            generator->bind_input(a.name, to_input_vector<Expr>(o, a.name));
        }
    };

    for (auto kw : kwargs) {
        const std::string name = kw.first.cast<std::string>();
        const py::handle value = kw.second;

        if (name == "generator_params") {
            continue;
        }

        auto it = input_arguments_map.find(name);
        _halide_user_assert(it != input_arguments_map.end()) << "Unknown input '" << name << "' specified via keyword argument.";
        _halide_user_assert(inputs_seen.count(name) == 0) << "Input " << name << " specified multiple times.";
        inputs_seen.insert(name);

        const auto &a = it->second;
        bind_one(value, a);
        kw_inputs_specified++;
    }

    if (args.empty()) {
        // No arguments specified positionally, so they must all be via keywords.
        _halide_user_assert(kw_inputs_specified == input_arguments.size())
            << "Expected exactly " << input_arguments.size() << " keyword args for inputs, but saw " << kw_inputs_specified << ".";
    } else {
        // Some positional arguments, so all inputs must be positional (and none via keyword).
        _halide_user_assert(kw_inputs_specified == 0) << "Cannot use both positional and keyword arguments for inputs.";
        _halide_user_assert(args.size() == input_arguments.size())
            << "Expected exactly " << input_arguments.size() << " positional args for inputs, but saw " << args.size() << ".";
        for (size_t i = 0; i < args.size(); i++) {
            const auto &a = input_arguments[i];
            _halide_user_assert(inputs_seen.count(a.name) == 0) << "Input " << a.name << " specified multiple times.";
            inputs_seen.insert(a.name);
            bind_one(args[i], a);
        }
    }

    generator->build_pipeline();

    const size_t outputs_size = output_arguments.size();
    py::tuple py_outputs(outputs_size);
    for (size_t i = 0; i < outputs_size; i++) {
        std::vector<Func> outputs = generator->output_func(output_arguments[i].name);

        py::object o;
        if (outputs.size() == 1) {
            // convert list-of-1 into single element
            o = py::cast(outputs[0]);
        } else {
            o = py::cast(outputs);
        }
        if (outputs_size == 1) {
            // bail early, returning the single object rather than a dict
            return o;
        }
        py_outputs[i] = o;
    }

    // An explicit "std::move" is needed here because there's
    // an implicit tuple->object conversion that inhibits it otherwise.
    return std::move(py_outputs);
}

void pystub_init(pybind11::module &m, const GeneratorFactory &factory) {
    m.def(
        "generate", [factory](const Halide::Target &target, const py::args &args, const py::kwargs &kwargs) -> py::object {
            return generate_impl(factory, Halide::GeneratorContext(target), args, kwargs);
        },
        py::arg("target"));
}

}  // namespace
}  // namespace PythonBindings
}  // namespace Halide

extern "C" PyObject *_halide_pystub_impl(const char *module_name, const Halide::Internal::GeneratorFactory &factory) {
    int major, minor;
    if (sscanf(Py_GetVersion(), "%i.%i", &major, &minor) != 2) {
        PyErr_SetString(PyExc_ImportError, "Can't parse Python version.");
        return nullptr;
    } else if (major != PY_MAJOR_VERSION || minor != PY_MINOR_VERSION) {
        PyErr_Format(PyExc_ImportError,
                     "Python version mismatch: module was compiled for "
                     "version %i.%i, while the interpreter is running "
                     "version %i.%i.",
                     PY_MAJOR_VERSION, PY_MINOR_VERSION,
                     major, minor);
        return nullptr;
    }

    // TODO: do something meaningful with the PyModuleDef & add a doc string
    auto m = pybind11::module_::create_extension_module(module_name, nullptr, new PyModuleDef());
    try {
        Halide::PythonBindings::install_error_handlers(m);
        Halide::PythonBindings::pystub_init(m, factory);
        return m.ptr();
    } catch (pybind11::error_already_set &e) {
        PyErr_SetString(PyExc_ImportError, e.what());
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_ImportError, e.what());
        return nullptr;
    }
}
