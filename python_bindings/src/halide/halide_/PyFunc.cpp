#include "PyFunc.h"

#include <utility>

#include "PyBuffer.h"
#include "PyExpr.h"
#include "PyFuncRef.h"
#include "PyLoopLevel.h"
#include "PyScheduleMethods.h"
#include "PyStage.h"
#include "PyTuple.h"
#include "PyVarOrRVar.h"

namespace Halide {
namespace PythonBindings {

namespace {

template<typename LHS>
void define_get(py::class_<Func> &func_class) {
    func_class
        .def("__getitem__",
             [](Func &func, const LHS &args) -> FuncRef {
                 return func(args);
             });
}

template<typename LHS, typename RHS>
void define_set(py::class_<Func> &func_class) {
    func_class
        .def("__setitem__",
             [](Func &func, const LHS &lhs, const RHS &rhs) -> Stage {
                 return func(lhs) = rhs;
             })
        .def("__setitem__",
             [](Func &func, const std::vector<LHS> &lhs, const RHS &rhs) -> Stage {
                 return func(lhs) = rhs;
             });
}

// See the usages below this function to see why we are specializing this function.
template<typename RHS>
void define_set_func_ref(py::class_<Func> &func_class) {
    func_class
        .def("__setitem__",
             [](Func &func, const FuncRef &lhs, const RHS &rhs) -> Stage {
                 return func(lhs) = rhs;
             });
}

// Special case: there is no implicit conversion of double in C++ Halide
// but there is implicit conversion of double in Python.
template<>
void define_set_func_ref<double>(py::class_<Func> &func_class) {
    func_class
        .def("__setitem__",
             [](Func &func, const FuncRef &lhs, const double &rhs) -> Stage {
                 // Implicitly convert rhs to single precision. Issue warnings if
                 // we detect loss of precision.
                 float f = rhs;
                 if (Internal::reinterpret_bits<uint64_t>(rhs) !=
                     Internal::reinterpret_bits<uint64_t>((double)f)) {
                     double diff = rhs - f;
                     std::ostringstream os;
                     os << "Loss of precision detected when casting " << rhs << " to a single precision float. The difference is " << diff << ".";
                     std::string msg = os.str();
                     PyErr_WarnEx(nullptr, msg.c_str(), 1);
                 }
                 return func(lhs) = Expr(f);
             });
}

py::object realization_to_object(const Realization &r) {
    // Only one Buffer -> just return it
    if (r.size() == 1) {
        return py::cast(r[0]);
    }

    // Multiple -> return as Python tuple
    return to_python_tuple(r);
}

py::object evaluate_impl(const py::object &expr, bool may_gpu) {
    Tuple t = to_halide_tuple(expr);
    Func f("evaluate_func_" + std::to_string(t.size()));
    f() = t;
    if (may_gpu) {
        Internal::schedule_scalar(f);
    }

    std::optional<Realization> r;
    {
        py::gil_scoped_release release;

        r = f.realize();
    }
    if (r->size() == 1) {
        return buffer_getitem_operator((*r)[0], {});
    } else {
        py::tuple result(r->size());
        for (size_t i = 0; i < r->size(); i++) {
            result[i] = buffer_getitem_operator((*r)[i], {});
        }
        return result;
    }
}

}  // namespace

void define_func(py::module &m) {
    define_func_ref(m);
    define_var_or_rvar(m);
    define_loop_level(m);

    // Deliberately not supported, because they don't seem to make sense for Python:
    // - set_custom_allocator()
    // - set_custom_do_task()
    // - set_custom_do_par_for()
    // - jit_handlers()
    // - add_custom_lowering_pass()
    // - clear_custom_lowering_passes()
    // - custom_lowering_passes()

    // Not supported yet, because we want to think about how to expose runtime
    // overrides in Python (https://github.com/halide/Halide/issues/2790):
    // - set_error_handler()
    // - set_custom_trace()
    // - set_custom_print()
    // - JITUserContext

    auto func_class =
        py::class_<Func>(m, "Func")
            .def(py::init<>())
            .def(py::init<std::string>())
            .def(py::init<Type, int, std::string>(), py::arg("required_type"), py::arg("required_dimensions"), py::arg("name"))
            .def(py::init<std::vector<Type>, int, std::string>(), py::arg("required_types"), py::arg("required_dimensions"), py::arg("name"))
            .def(py::init<Expr>())
            .def(py::init([](Buffer<> &b) -> Func { return Func(b); }))

            // for implicitly_convertible
            .def(py::init([](const ImageParam &im) -> Func { return im; }))

            .def(
                "realize",
                [](Func &f, Buffer<> buffer, const Target &target) -> void {
                    py::gil_scoped_release release;
                    f.realize(buffer, target);
                },
                py::arg("dst"), py::arg("target") = Target())

            // It's important to have this overload of realize() go first:
            // passing an empty list [] is ambiguous in Python, and could match to
            // either list-of-sizes or list-of-buffers... but the former is useful
            // (it allows realizing a 0-dimensional/scalar buffer) and the former is
            // not (it will always assert-fail). Putting this one first allows it to
            // be the first one chosen by the bindings in this case.
            .def(
                "realize",
                [](Func &f, const std::vector<int32_t> &sizes, const Target &target) -> py::object {
                    std::optional<Realization> r;
                    {
                        py::gil_scoped_release release;
                        r = f.realize(sizes, target);
                    }
                    return realization_to_object(*r);
                },
                py::arg("sizes") = std::vector<int32_t>{}, py::arg("target") = Target())

            // This will actually allow a list-of-buffers as well as a tuple-of-buffers, but that's OK.
            .def(
                "realize",
                [](Func &f, std::vector<Buffer<>> buffers, const Target &t) -> void {
                    py::gil_scoped_release release;
                    f.realize(Realization(std::move(buffers)), t);
                },
                py::arg("dst"), py::arg("target") = Target())

            .def("defined", &Func::defined)
            .def("name", &Func::name)
            .def("dimensions", &Func::dimensions)
            .def("args", &Func::args)
            .def("value", &Func::value)
            .def("values", [](Func &func) -> py::tuple {
                return to_python_tuple(func.values());
            })
            .def("defined", &Func::defined)
            .def("outputs", &Func::outputs)

            .def("type", &Func::type)
            .def("types", &Func::types)

            .def("bound", &Func::bound, py::arg("var"), py::arg("min"), py::arg("extent"))

            .def("reorder_storage", (Func & (Func::*)(const std::vector<Var> &)) & Func::reorder_storage, py::arg("dims"))
            .def("reorder_storage", [](Func &func, const py::args &args) -> Func & {
                return func.reorder_storage(args_to_vector<Var>(args));
            })

            .def("compute_at", (Func & (Func::*)(const Func &, const Var &)) & Func::compute_at, py::arg("f"), py::arg("var"))
            .def("compute_at", (Func & (Func::*)(const Func &, const RVar &)) & Func::compute_at, py::arg("f"), py::arg("var"))
            .def("compute_at", (Func & (Func::*)(LoopLevel)) & Func::compute_at, py::arg("loop_level"))

            .def("store_at", (Func & (Func::*)(const Func &, const Var &)) & Func::store_at, py::arg("f"), py::arg("var"))
            .def("store_at", (Func & (Func::*)(const Func &, const RVar &)) & Func::store_at, py::arg("f"), py::arg("var"))
            .def("store_at", (Func & (Func::*)(LoopLevel)) & Func::store_at, py::arg("loop_level"))

            .def("async_", &Func::async)
            .def("memoize", &Func::memoize)
            .def("compute_inline", &Func::compute_inline)
            .def("compute_root", &Func::compute_root)
            .def("store_root", &Func::store_root)

            .def("store_in", &Func::store_in, py::arg("memory_type"))

            .def("compile_to", &Func::compile_to, py::arg("outputs"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())

            .def("compile_to_bitcode", (void(Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) & Func::compile_to_bitcode, py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_bitcode", (void(Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) & Func::compile_to_bitcode, py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

            .def("compile_to_llvm_assembly", (void(Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) & Func::compile_to_llvm_assembly, py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_llvm_assembly", (void(Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) & Func::compile_to_llvm_assembly, py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

            .def("compile_to_object", (void(Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) & Func::compile_to_object, py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_object", (void(Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) & Func::compile_to_object, py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

            .def("compile_to_header", &Func::compile_to_header, py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

            .def("compile_to_assembly", (void(Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) & Func::compile_to_assembly, py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
            .def("compile_to_assembly", (void(Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) & Func::compile_to_assembly, py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

            .def("compile_to_c", &Func::compile_to_c, py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

            .def("compile_to_lowered_stmt", &Func::compile_to_lowered_stmt, py::arg("filename"), py::arg("arguments"), py::arg("fmt") = Text, py::arg("target") = get_target_from_environment())

            .def("compile_to_file", &Func::compile_to_file, py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

            .def("compile_to_static_library", &Func::compile_to_static_library, py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

            .def("compile_to_multitarget_static_library", &Func::compile_to_multitarget_static_library, py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"))
            .def("compile_to_multitarget_object_files", &Func::compile_to_multitarget_object_files, py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"), py::arg("suffixes"))

            // TODO: useless until Module is defined.
            .def("compile_to_module", &Func::compile_to_module, py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

            .def("compile_jit", &Func::compile_jit, py::arg("target") = get_jit_target_from_environment())

            .def("compile_to_callable", &Func::compile_to_callable, py::arg("arguments"), py::arg("target") = get_jit_target_from_environment())

            .def("has_update_definition", &Func::has_update_definition)
            .def("num_update_definitions", &Func::num_update_definitions)

            .def("update", &Func::update, py::arg("idx") = 0)
            .def("update_args", &Func::update_args, py::arg("idx") = 0)
            .def("update_value", &Func::update_value, py::arg("idx") = 0)
            .def("update_value", &Func::update_value, py::arg("idx") = 0)
            .def(
                "update_values", [](Func &func, int idx) -> py::tuple {
                    return to_python_tuple(func.update_values(idx));
                },
                py::arg("idx") = 0)
            .def("rvars", &Func::rvars, py::arg("idx") = 0)

            .def("trace_loads", &Func::trace_loads)
            .def("trace_stores", &Func::trace_stores)
            .def("trace_realizations", &Func::trace_realizations)
            .def("print_loop_nest", &Func::print_loop_nest)
            .def("add_trace_tag", &Func::add_trace_tag, py::arg("trace_tag"))

            // TODO: also provide to-array versions to avoid requiring filesystem usage
            .def("debug_to_file", &Func::debug_to_file)

            .def("is_extern", &Func::is_extern)
            .def("extern_function_name", &Func::extern_function_name)

            .def("define_extern", (void(Func::*)(const std::string &, const std::vector<ExternFuncArgument> &, const std::vector<Type> &, const std::vector<Var> &, NameMangling, DeviceAPI)) & Func::define_extern, py::arg("function_name"), py::arg("params"), py::arg("types"), py::arg("arguments"), py::arg("mangling") = NameMangling::Default, py::arg("device_api") = DeviceAPI::Host)

            .def("define_extern", (void(Func::*)(const std::string &, const std::vector<ExternFuncArgument> &, Type, int, NameMangling, DeviceAPI)) & Func::define_extern, py::arg("function_name"), py::arg("params"), py::arg("type"), py::arg("dimensionality"), py::arg("mangling") = NameMangling::Default, py::arg("device_api") = DeviceAPI::Host)

            .def("define_extern", (void(Func::*)(const std::string &, const std::vector<ExternFuncArgument> &, const std::vector<Type> &, int, NameMangling, DeviceAPI)) & Func::define_extern, py::arg("function_name"), py::arg("params"), py::arg("types"), py::arg("dimensionality"), py::arg("mangling") = NameMangling::Default, py::arg("device_api") = DeviceAPI::Host)

            .def("define_extern", (void(Func::*)(const std::string &, const std::vector<ExternFuncArgument> &, Type, const std::vector<Var> &, NameMangling, DeviceAPI)) & Func::define_extern, py::arg("function_name"), py::arg("params"), py::arg("type"), py::arg("arguments"), py::arg("mangling") = NameMangling::Default, py::arg("device_api") = DeviceAPI::Host)

            .def("output_buffer", &Func::output_buffer)
            .def("output_buffers", &Func::output_buffers)

            .def(
                "infer_input_bounds", [](Func &f, const py::object &dst, const Target &target) -> void {
                    // dst could be Buffer<>, vector<Buffer>, or vector<int>
                    try {
                        Buffer<> b = dst.cast<Buffer<>>();
                        f.infer_input_bounds(b, target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<Buffer<>> v = dst.cast<std::vector<Buffer<>>>();
                        f.infer_input_bounds(Realization(std::move(v)), target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    try {
                        std::vector<int32_t> v = dst.cast<std::vector<int32_t>>();
                        f.infer_input_bounds(v, target);
                        return;
                    } catch (...) {
                        // fall thru
                    }

                    throw py::value_error("Invalid arguments to infer_input_bounds");
                },
                py::arg("dst"), py::arg("target") = get_jit_target_from_environment())

            .def("in_", (Func(Func::*)(const Func &)) & Func::in, py::arg("f"))
            .def("in_", (Func(Func::*)(const std::vector<Func> &fs)) & Func::in, py::arg("fs"))
            .def("in_", (Func(Func::*)()) & Func::in)

            .def("clone_in", (Func(Func::*)(const Func &)) & Func::clone_in, py::arg("f"))
            .def("clone_in", (Func(Func::*)(const std::vector<Func> &fs)) & Func::clone_in, py::arg("fs"))

            .def("copy_to_device", &Func::copy_to_device, py::arg("device_api") = DeviceAPI::Default_GPU)
            .def("copy_to_host", &Func::copy_to_host)

            .def("set_estimate", &Func::set_estimate, py::arg("var"), py::arg("min"), py::arg("extent"))
            .def("set_estimates", &Func::set_estimates, py::arg("estimates"))

            .def("align_bounds", &Func::align_bounds, py::arg("var"), py::arg("modulus"), py::arg("remainder") = 0)
            .def("align_extent", &Func::align_extent, py::arg("var"), py::arg("modulus"))

            .def("bound_extent", &Func::bound_extent, py::arg("var"), py::arg("extent"))

            .def("align_storage", &Func::align_storage, py::arg("dim"), py::arg("alignment"))

            .def("fold_storage", &Func::fold_storage, py::arg("dim"), py::arg("extent"), py::arg("fold_forward") = true)

            .def("infer_arguments", &Func::infer_arguments)

            .def("__repr__", [](const Func &func) -> std::string {
                std::ostringstream o;
                o << "<halide.Func '" << func.name() << "'>";
                return o.str();
            });

    py::implicitly_convertible<ImageParam, Func>();

    // Note that overloads of FuncRef must come *before* Expr;
    // otherwise PyBind's automatic STL conversion machinery
    // can attempt to convert a FuncRef into a vector-of-size-1 Expr,
    // which will fail. TODO: can we avoid this?

    // Ordinary calls to Funcs
    define_get<FuncRef>(func_class);
    define_get<Expr>(func_class);
    define_get<std::vector<Expr>>(func_class);
    define_get<Var>(func_class);
    define_get<std::vector<Var>>(func_class);

    // Special cases of f[g[...]] = ...
    // We need to capture this case here since otherwise
    // pybind11 would try to convert g[...], which is a FuncRef
    // to a list of Var.
    // However, to do this it has to check g[...][0] is a Var or not.
    // If g is not defined as a Tuple this results in a runtime error
    // inside Halide.
    // We also can't rely on implicit conversion in pybind11 since it
    // happens after all function overloads have been visited.
    define_set_func_ref<FuncRef>(func_class);
    define_set_func_ref<Expr>(func_class);
    define_set_func_ref<Tuple>(func_class);
    define_set_func_ref<int>(func_class);
    define_set_func_ref<double>(func_class);

    // LHS(Var, ...Var) is LHS of an ordinary Func definition.
    define_set<Var, FuncRef>(func_class);
    define_set<Var, Expr>(func_class);
    define_set<Var, Tuple>(func_class);
    // define_set<Var, std::vector<Var>>(func_class);

    // LHS(Expr, ...Expr) can only be LHS of an update definition.
    define_set<Expr, FuncRef>(func_class);
    define_set<Expr, Expr>(func_class);
    define_set<Expr, Tuple>(func_class);

    add_schedule_methods(func_class);

    define_stage(m);

    m.def(
        "evaluate", [](const py::object &expr) -> py::object {
            return evaluate_impl(expr, false);
        },
        py::arg("expr"));

    m.def(
        "evaluate_may_gpu", [](const py::object &expr) -> py::object {
            return evaluate_impl(expr, true);
        },
        py::arg("expr"));
}

}  // namespace PythonBindings
}  // namespace Halide
