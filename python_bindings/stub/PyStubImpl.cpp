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

namespace py = pybind11;

using FactoryFunc = std::unique_ptr<Halide::Internal::GeneratorBase> (*)(const Halide::GeneratorContext &context);

namespace Halide {
namespace PythonBindings {

using GeneratorParamsMap = Internal::GeneratorParamsMap;
using Stub = Internal::GeneratorStub;
using StubInput = Internal::StubInput;
using StubInputBuffer = Internal::StubInputBuffer<void>;

namespace {

// This seems redundant to the code in PyError.cpp, but is necessary
// in case the Stub builder links in a separate copy of libHalide, rather
// sharing the same halide.so that is built by default.
void halide_python_error(void *, const char *msg) {
    throw Error(msg);
}

void halide_python_print(void *, const char *msg) {
    py::print(msg, py::arg("end") = "");
}

class HalidePythonCompileTimeErrorReporter : public CompileTimeErrorReporter {
public:
    void warning(const char *msg) override {
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

    Halide::Internal::JITHandlers handlers;
    handlers.custom_error = halide_python_error;
    handlers.custom_print = halide_python_print;
    Halide::Internal::JITSharedRuntime::set_default_handlers(handlers);
}

// Anything that defines __getitem__ looks sequencelike to pybind,
// so also check for __len_ to avoid things like Buffer and Func here.
bool is_real_sequence(const py::object &o) {
    return py::isinstance<py::sequence>(o) && py::hasattr(o, "__len__");
}

StubInput to_stub_input(const py::object &o) {
    // Don't use isinstance: we want to get things that
    // can be implicitly converted as well (eg ImageParam -> Func)
    try {
        return StubInput(StubInputBuffer(o.cast<Buffer<>>()));
    } catch (...) {
        // Not convertible to Buffer. Fall thru and try next.
    }

    try {
        return StubInput(o.cast<Func>());
    } catch (...) {
        // Not convertible to Func. Fall thru and try next.
    }

    return StubInput(o.cast<Expr>());
}

void append_input(const py::object &value, std::vector<StubInput> &v) {
    if (is_real_sequence(value)) {
        for (auto o : py::reinterpret_borrow<py::sequence>(value)) {
            v.push_back(to_stub_input(o));
        }
    } else {
        v.push_back(to_stub_input(value));
    }
}

py::object generate_impl(FactoryFunc factory, const GeneratorContext &context, const py::args &args, const py::kwargs &kwargs) {
    Stub stub(context, [factory](const GeneratorContext &context) -> std::unique_ptr<Halide::Internal::GeneratorBase> {
        return factory(context);
    });
    auto names = stub.get_names();
    _halide_user_assert(!names.outputs.empty())
        << "Generators that use build() (instead of generate()+Output<>) are not supported in the Python bindings.";
    std::map<std::string, size_t> input_name_to_pos;
    for (size_t i = 0; i < names.inputs.size(); ++i) {
        input_name_to_pos[names.inputs[i]] = i;
    }

    // Inputs can be specified by either positional or named args,
    // and must all be specified.
    //
    // GeneratorParams can only be specified by name, and are always optional.
    //
    std::vector<std::vector<StubInput>> inputs;
    inputs.resize(names.inputs.size());

    GeneratorParamsMap generator_params;

    // Process the kwargs first.
    for (auto kw : kwargs) {
        // If the kwarg is the name of a known input, stick it in the input
        // vector. If not, stick it in the GeneratorParamsMap (if it's invalid,
        // an error will be reported further downstream).
        std::string key = kw.first.cast<std::string>();
        py::handle value = kw.second;
        auto it = input_name_to_pos.find(key);
        if (it != input_name_to_pos.end()) {
            append_input(py::cast<py::object>(value), inputs[it->second]);
        } else {
            if (py::isinstance<LoopLevel>(value)) {
                generator_params[key] = value.cast<LoopLevel>();
            } else {
                generator_params[key] = py::str(value).cast<std::string>();
            }
        }
    }

    // Now, the positional args.
    _halide_user_assert(args.size() <= names.inputs.size())
        << "Expected at most " << names.inputs.size() << " positional args, but saw " << args.size() << ".";
    for (size_t i = 0; i < args.size(); ++i) {
        _halide_user_assert(inputs[i].empty())
            << "Generator Input named '" << names.inputs[i] << "' was specified by both position and keyword.";
        append_input(args[i], inputs[i]);
    }

    for (size_t i = 0; i < inputs.size(); ++i) {
        _halide_user_assert(!inputs[i].empty())
            << "Generator Input named '" << names.inputs[i] << "' was not specified.";
    }

    const std::vector<std::vector<Func>> outputs = stub.generate(generator_params, inputs);

    py::tuple py_outputs(outputs.size());
    for (size_t i = 0; i < outputs.size(); i++) {
        py::object o;
        if (outputs[i].size() == 1) {
            // convert list-of-1 into single element
            o = py::cast(outputs[i][0]);
        } else {
            o = py::cast(outputs[i]);
        }
        if (outputs.size() == 1) {
            // bail early, return the single object rather than a dict
            return o;
        }
        py_outputs[i] = o;
    }
    // An explicit "std::move" is needed here because there's
    // an implicit tuple->object conversion that inhibits it otherwise.
    return std::move(py_outputs);
}

void pystub_init(pybind11::module &m, FactoryFunc factory) {
    m.def(
        "generate", [factory](const Halide::Target &target, const py::args &args, const py::kwargs &kwargs) -> py::object {
            return generate_impl(factory, Halide::GeneratorContext(target), args, kwargs);
        },
        py::arg("target"));
}

}  // namespace
}  // namespace PythonBindings
}  // namespace Halide

extern "C" PyObject *_halide_pystub_impl(const char *module_name, FactoryFunc factory) {
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
    auto m = pybind11::module(module_name);
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
