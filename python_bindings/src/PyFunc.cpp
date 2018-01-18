#include "PyFunc.h"

#include "PyBinaryOperators.h"
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
        .def("__getitem__", [](Func &func, const LHS &args) -> FuncRef {
            return func(args);
        })
    ;
}

template<typename LHS, typename RHS>
void define_set(py::class_<Func> &func_class) {
    func_class
        .def("__setitem__", [](Func &func, const LHS &lhs, const RHS &rhs) -> Stage {
            return func(lhs) = rhs;
        })
        .def("__setitem__", [](Func &func, const std::vector<LHS> &lhs, const RHS &rhs) -> Stage {
            return func(lhs) = rhs;
        })
    ;
}

py::object realization_to_object(const Realization &r) {
    // Only one Buffer -> just return it
    if (r.size() == 1) {
        return py::cast(r[0]);
    }

    // Multiple -> return as Python tuple
    return to_python_tuple(r);
}

}  // namespace

void define_func(py::module &m) {
    define_func_ref(m);
    define_var_or_rvar(m);
    define_loop_level(m);

    // TODO: ParamMap to its own file?
    auto param_map_class = py::class_<ParamMap>(m, "ParamMap")
        .def(py::init<>())
    ;

    auto func_class = py::class_<Func>(m, "Func")
        .def(py::init<>())
        .def(py::init<std::string>())
        .def(py::init<Expr>())

        .def("realize", [](Func &f, Buffer<> buffer, const Target &target, const ParamMap &param_map) -> void {
            f.realize(Realization(buffer), target);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        // This will actually allow a list-of-buffers as well as a tuple-of-buffers, but that's OK.
        .def("realize", [](Func &f, std::vector<Buffer<>> buffers, const Target &t, const ParamMap &param_map) -> void {
            f.realize(Realization(buffers), t);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, std::vector<int32_t> sizes, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(sizes, target, param_map));
        }, py::arg("sizes"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(target, param_map));
        }, py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, int x_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(x_size, target, param_map));
        }, py::arg("x_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, int x_size, int y_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(x_size, y_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, int x_size, int y_size, int z_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(x_size, y_size, z_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("realize", [](Func &f, int x_size, int y_size, int z_size, int w_size, const Target &target, const ParamMap &param_map) -> py::object {
            return realization_to_object(f.realize(x_size, y_size, z_size, w_size, target, param_map));
        }, py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("w_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())

        .def("name", &Func::name)

        .def("output_types", &Func::output_types)

        .def("bound", &Func::bound)

        .def("reorder_storage", (Func &(Func::*)(const std::vector<Var> &)) &Func::reorder_storage, py::arg("dims"))
        .def("reorder_storage", [](Func &func, py::args args) -> Func & {
            return func.reorder_storage(args_to_vector<Var>(args));
        })

        .def("compute_at", (Func &(Func::*)(Func, Var)) &Func::compute_at)
        .def("compute_at", (Func &(Func::*)(Func, RVar)) &Func::compute_at)
        .def("compute_at", (Func &(Func::*)(LoopLevel)) &Func::compute_at)


        .def("store_at", (Func &(Func::*)(Func, Var)) &Func::store_at)
        .def("store_at", (Func &(Func::*)(Func, RVar)) &Func::store_at)
        .def("store_at", (Func &(Func::*)(LoopLevel)) &Func::store_at)

        .def("compute_root", &Func::compute_root)
        .def("store_root", &Func::store_root)

        .def("compile_to_bitcode",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) &Func::compile_to_bitcode,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
        .def("compile_to_bitcode",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) &Func::compile_to_bitcode,
            py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

        .def("compile_to_llvm_assembly",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) &Func::compile_to_llvm_assembly,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
        .def("compile_to_llvm_assembly",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) &Func::compile_to_llvm_assembly,
            py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

        .def("compile_to_object",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) &Func::compile_to_object,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
        .def("compile_to_object",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) &Func::compile_to_object,
            py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

        .def("compile_to_header", &Func::compile_to_header,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

        .def("compile_to_assembly",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const std::string &, const Target &target)) &Func::compile_to_assembly,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name"), py::arg("target") = get_target_from_environment())
        .def("compile_to_assembly",
            (void (Func::*)(const std::string &, const std::vector<Argument> &, const Target &target)) &Func::compile_to_assembly,
            py::arg("filename"), py::arg("arguments"), py::arg("target") = get_target_from_environment())

        .def("compile_to_c", &Func::compile_to_c,
            py::arg("filename"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

        .def("compile_to_lowered_stmt", &Func::compile_to_lowered_stmt,
            py::arg("filename"), py::arg("arguments"), py::arg("fmt") = Text, py::arg("target") = get_target_from_environment())

        .def("compile_to_file", &Func::compile_to_file,
            py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

        .def("compile_to_static_library", &Func::compile_to_static_library,
            py::arg("filename_prefix"), py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

        .def("compile_to_multitarget_static_library", &Func::compile_to_multitarget_static_library,
            py::arg("filename_prefix"), py::arg("arguments"), py::arg("targets"))

        .def("compile_to_module", &Func::compile_to_module,
            py::arg("arguments"), py::arg("fn_name") = "", py::arg("target") = get_target_from_environment())

        .def("compile_jit", &Func::compile_jit, py::arg("target") = get_jit_target_from_environment())

        .def("update", &Func::update, py::arg("idx") = 0)

        .def("trace_loads", &Func::trace_loads)
        .def("trace_stores", &Func::trace_stores)
        .def("trace_realizations", &Func::trace_realizations)
        .def("print_loop_nest", &Func::print_loop_nest)

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                const std::vector<Type> &, const std::vector<Var> &, NameMangling, DeviceAPI, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("types"),
             py::arg("arguments"), py::arg("mangling") = NameMangling::Default,
             py::arg("device_api") = DeviceAPI::Host,
             py::arg("uses_old_buffer_t") = false)

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                Type, int, NameMangling, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("type"),
             py::arg("dimensionality"), py::arg("mangling"),
             py::arg("uses_old_buffer_t"))


        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                Type, int, NameMangling, DeviceAPI, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("type"),
             py::arg("dimensionality"), py::arg("mangling") = NameMangling::Default,
             py::arg("device_api") = DeviceAPI::Host,
             py::arg("uses_old_buffer_t") = false)

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                const std::vector<Type> &, int, NameMangling, DeviceAPI, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("types"),
             py::arg("dimensionality"), py::arg("mangling") = NameMangling::Default,
             py::arg("device_api") = DeviceAPI::Host,
             py::arg("uses_old_buffer_t") = false)

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                Type, const std::vector<Var> &, NameMangling, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("type"),
             py::arg("arguments"), py::arg("mangling"),
             py::arg("uses_old_buffer_t"))

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                Type, const std::vector<Var> &, NameMangling, DeviceAPI, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("type"),
             py::arg("arguments"), py::arg("mangling") = NameMangling::Default,
             py::arg("device_api") = DeviceAPI::Host,
             py::arg("uses_old_buffer_t") = false)

        .def("define_extern", (void (Func::*)(const std::string &, const std::vector<ExternFuncArgument> &,
                const std::vector<Type> &, const std::vector<Var> &, NameMangling, bool)) &Func::define_extern,
             py::arg("function_name"), py::arg("params"), py::arg("types"),
             py::arg("arguments"), py::arg("mangling"),
             py::arg("uses_old_buffer_t"))

        .def("__repr__", [](const Func &func) -> std::string {
            std::ostringstream o;
            o << "<halide.Func '" << func.name() << "'>";
            return o.str();
        })
    ;

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

    // LHS(Var, ...Var) is LHS of an ordinary Func definition.
    define_set<Var, FuncRef>(func_class);
    define_set<Var, Expr>(func_class);
    define_set<Var, Tuple>(func_class);
    //define_set<Var, std::vector<Var>>(func_class);

    // LHS(Expr, ...Expr) can only be LHS of an update definition.
    define_set<Expr, FuncRef>(func_class);
    define_set<Expr, Expr>(func_class);
    define_set<Expr, Tuple>(func_class);

    add_schedule_methods(func_class);

    define_stage(m);
}

}  // namespace PythonBindings
}  // namespace Halide
