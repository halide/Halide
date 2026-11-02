#include "PyStub.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include <vector>

namespace Halide {
namespace PythonBindings {

using ArgInfoKind = Internal::ArgInfoKind;
using ArgInfo = Internal::AbstractGenerator::ArgInfo;
using GeneratorFactory = Internal::GeneratorFactory;
using StubInput = Internal::StubInput;
using StubInputBuffer = Internal::StubInputBuffer<void>;

namespace {

// Also check for __len__ to avoid things like Buffer and Func here.
bool is_real_sequence(const py::object &o) {
    return py::isinstance<py::sequence>(o) && py::hasattr(o, "__len__");
}

template<typename T>
struct cast_error_string {
    std::string operator()(const py::handle &h, const std::string &name) {
        return "Unable to cast Input " + name + " to " + py::cast<std::string>(py::str(py::type<T>())) + " from " + py::cast<std::string>(py::str(h.type()));
    }
};

template<>
std::string cast_error_string<Buffer<>>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires an ImageParam or Buffer argument when using call(), but saw " << py::cast<std::string>(py::str(h.type()));
    return o.str();
}

template<>
std::string cast_error_string<Func>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires a Func argument when using call(), but saw " << py::cast<std::string>(py::str(h.type()));
    return o.str();
}

template<>
std::string cast_error_string<Expr>::operator()(const py::handle &h, const std::string &name) {
    std::ostringstream o;
    o << "Input " << name << " requires a Param (or scalar literal) argument when using call(), but saw " << py::cast<std::string>(py::str(h.type()));
    return o.str();
}

template<typename T>
T cast_to(const py::handle &h, const std::string &name) {
    // We want to ensure that the error thrown is one that will be translated
    // to `hl.HalideError` in Python.
    try {
        return py::cast<T>(h);
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
        for (const auto &o : py::borrow<py::sequence>(value)) {
            v.push_back(cast_to<T>(o, name));
        }
    } else {
        v.push_back(cast_to<T>(value, name));
    }
    return v;
}

py::object call_impl(const GeneratorFactory &factory,
                     const py::args &args,
                     const py::kwargs &kwargs) {
    auto active_generator_context = py::module_::import_("halide").attr("active_generator_context");
    auto context = py::cast<GeneratorContext>(active_generator_context());
    auto generator = factory(context);

    // GeneratorParams are always specified as an optional named parameter
    // called "generator_params", which is expected to be a python dict.
    // If generatorparams are specified, do them first, before any Inputs.
    if (kwargs.contains("generator_params")) {
        py::handle h = kwargs["generator_params"];
        _halide_user_assert(py::isinstance<py::dict>(h)) << "generator_params must be a dict";
        py::dict gp = py::cast<py::dict>(h);
        for (auto item : gp) {
            const std::string gp_name = py::cast<std::string>(py::str(item.first));
            const py::handle gp_value = item.second;
            if (py::isinstance<LoopLevel>(gp_value)) {
                // Note that while Python Generators don't support LoopLevels,
                // C++ Generators do, and that's what we're calling here, so
                // be sure to allow passing 'em in.
                generator->set_generatorparam_value(gp_name, py::cast<LoopLevel>(gp_value));
            } else if (py::isinstance<py::list>(gp_value)) {
                // Convert [hl.UInt(8), hl.Int(16)] -> uint8,int16
                std::string v;
                for (auto t : gp_value) {
                    if (!v.empty()) {
                        v += ",";
                    }
                    v += py::cast<std::string>(py::str(t));
                }
                generator->set_generatorparam_value(gp_name, v);
            } else {
                generator->set_generatorparam_value(gp_name, py::cast<std::string>(py::str(gp_value)));
            }
        }
    }

    // Don't call arginfos() until after we have set all GeneratorParams.

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

    _halide_user_assert(args.size() <= input_arguments.size()) << "Generator '" << generator->name()
                                                               << "' allows at most " << input_arguments.size()
                                                               << " positional args, but " << args.size() << " were specified.";

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

    for (size_t i = 0; i < args.size(); i++) {
        const auto &a = input_arguments[i];
        _halide_user_assert(inputs_seen.count(a.name) == 0) << "Input " << a.name << " specified multiple times.";
        inputs_seen.insert(a.name);
        bind_one(args[i], a);
    }

    for (auto kw : kwargs) {
        const std::string name = py::cast<std::string>(kw.first);
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
    }

    _halide_user_assert(inputs_seen.size() == input_arguments.size()) << "Generator '" << generator->name()
                                                                      << "' requires " << input_arguments.size()
                                                                      << " args, but " << inputs_seen.size() << " were specified.";

    generator->build_pipeline();

    const size_t outputs_size = output_arguments.size();
    auto py_outputs = py::steal<py::tuple>(PyTuple_New(outputs_size));
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
        PyTuple_SetItem(py_outputs.ptr(), i, o.release().ptr());
    }

    // An explicit "std::move" is needed here because there's
    // an implicit tuple->object conversion that inhibits it otherwise.
    return std::move(py_outputs);
}

}  // namespace

void define_pystub(py::module_ &m) {
    m.def("_initialize_pystub", [](py::module_ &module, const py::capsule &capsule) {
        const auto *factory = static_cast<const GeneratorFactory *>(
            PyCapsule_GetPointer(capsule.ptr(), "halide.generator_factory"));
        if (!factory) {
            throw py::python_error();
        }
        module.def(
            "call", [factory = *factory](const py::args &args, const py::kwargs &kwargs) -> py::object {
                return call_impl(factory, args, kwargs);
            });
    });
}

}  // namespace PythonBindings
}  // namespace Halide
