#include "PyFunc.h"

#include "PyBinaryOperators.h"
#include "PyBuffer.h"
#include "PyExpr.h"
#include "PyFuncRef.h"
#include "PyScheduleMethods.h"
#include "PyStage.h"
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

}  // namespace

void define_func(py::module &m) {
    py::enum_<StmtOutputFormat>(m, "StmtOutputFormat")
        .value("Text", StmtOutputFormat::Text)
        .value("HTML", StmtOutputFormat::HTML);

    py::enum_<TailStrategy>(m, "TailStrategy")
        .value("RoundUp", TailStrategy::RoundUp)
        .value("GuardWithIf", TailStrategy::GuardWithIf)
        .value("ShiftInwards", TailStrategy::ShiftInwards)
        .value("Auto", TailStrategy::Auto)
    ;

    py::enum_<NameMangling>(m, "NameMangling")
        .value("Default", NameMangling::Default)
        .value("C", NameMangling::C)
        .value("CPlusPlus", NameMangling::CPlusPlus)
    ;

    define_func_ref(m);
    define_var_or_rvar(m);

    // TODO: LoopLevel to its own file?
    auto looplevel_class = py::class_<LoopLevel>(m, "LoopLevel")
        .def(py::init<>())
        .def("inlined", &LoopLevel::inlined)
        .def("root", &LoopLevel::root)
    ;

    // TODO: ParamMap to its own file?
    auto param_map_class = py::class_<ParamMap>(m, "ParamMap")
        .def(py::init<>())
    ;

    auto func_class = py::class_<Func>(m, "Func")
        .def(py::init<>())
        .def(py::init<std::string>())
        .def(py::init<Expr>())
        // TODO: this overload shouldn't be necessary, but for reasons that
        // aren't clear, PyBind isn't doing the implicit Buffer -> Realization
        // conversion here, and is choosing the std::vector<int> variant, which
        // crashes. Adding an explicit overload for Buffer<> heals it.
        .def("realize", [](Func &f, Buffer<> b, const Target &t, const ParamMap &param_map) -> void {
            f.realize(Realization(b), t);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", [](Func &f, std::vector<Buffer<>> b, const Target &t, const ParamMap &param_map) -> void {
            f.realize(Realization(b), t);
        }, py::arg("dst"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(std::vector<int32_t>, const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("sizes"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(int, const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("x_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(int, int, const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("x_size"), py::arg("y_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(int, int, int, const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
        .def("realize", (Realization (Func::*)(int, int, int, int, const Target &, const ParamMap &param_map)) &Func::realize,
            py::arg("x_size"), py::arg("y_size"), py::arg("z_size"), py::arg("w_size"), py::arg("target") = Target(), py::arg("param_map") = ParamMap())
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
