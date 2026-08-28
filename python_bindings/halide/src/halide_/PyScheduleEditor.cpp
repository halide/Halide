#include "PyScheduleEditor.h"

namespace Halide {
namespace PythonBindings {

namespace {

// Fluent StageHandle methods mutate the handle in place (advancing its
// insertion cursor) and return the handle by value; because ScheduleEditor is a
// reference-counted handle, the returned copy shares the same directive list,
// so both `h.split(...).vectorize(...)` and separate statements work.
template<typename Fn>
StageHandle chain(StageHandle &s, Fn &&fn) {
    std::forward<Fn>(fn)(s);
    return s;
}

}  // namespace

void define_schedule_editor(py::module &m) {
    // ---- VarSpec ----
    py::class_<VarSpec>(m, "VarSpec")
        .def(py::init<>())
        .def(py::init([](const std::string &name) { return VarSpec(name); }), py::arg("name"))
        .def(py::init<std::string, bool>(), py::arg("name"), py::arg("is_rvar"))
        .def(py::init([](const Var &v) { return VarSpec(v); }), py::arg("var"))
        .def(py::init([](const RVar &r) { return VarSpec(r); }), py::arg("rvar"))
        .def_readwrite("name", &VarSpec::name)
        .def_readwrite("is_rvar", &VarSpec::is_rvar)
        .def("__repr__", [](const VarSpec &v) {
            return "<halide.VarSpec '" + v.name + "'>";
        });
    py::implicitly_convertible<std::string, VarSpec>();
    py::implicitly_convertible<Var, VarSpec>();
    py::implicitly_convertible<RVar, VarSpec>();

    // ---- ScheduleDirective ----
    auto directive = py::class_<ScheduleDirective>(m, "ScheduleDirective");

    py::enum_<ScheduleDirective::Kind>(directive, "Kind")
        .value("Split", ScheduleDirective::Kind::Split)
        .value("Fuse", ScheduleDirective::Kind::Fuse)
        .value("Rename", ScheduleDirective::Kind::Rename)
        .value("Reorder", ScheduleDirective::Kind::Reorder)
        .value("Tile", ScheduleDirective::Kind::Tile)
        .value("Serial", ScheduleDirective::Kind::Serial)
        .value("Parallel", ScheduleDirective::Kind::Parallel)
        .value("Vectorize", ScheduleDirective::Kind::Vectorize)
        .value("Unroll", ScheduleDirective::Kind::Unroll)
        .value("Atomic", ScheduleDirective::Kind::Atomic)
        .value("AllowRaceConditions", ScheduleDirective::Kind::AllowRaceConditions)
        .value("GpuBlocks", ScheduleDirective::Kind::GpuBlocks)
        .value("GpuThreads", ScheduleDirective::Kind::GpuThreads)
        .value("GpuLanes", ScheduleDirective::Kind::GpuLanes)
        .value("GpuTile", ScheduleDirective::Kind::GpuTile)
        .value("Gpu", ScheduleDirective::Kind::Gpu)
        .value("GpuSingleThread", ScheduleDirective::Kind::GpuSingleThread)
        .value("Hexagon", ScheduleDirective::Kind::Hexagon)
        .value("Partition", ScheduleDirective::Kind::Partition)
        .value("NeverPartition", ScheduleDirective::Kind::NeverPartition)
        .value("NeverPartitionAll", ScheduleDirective::Kind::NeverPartitionAll)
        .value("AlwaysPartition", ScheduleDirective::Kind::AlwaysPartition)
        .value("AlwaysPartitionAll", ScheduleDirective::Kind::AlwaysPartitionAll)
        .value("Host", ScheduleDirective::Kind::Host)
        .value("SmeStreaming", ScheduleDirective::Kind::SmeStreaming)
        .value("StreamLoads", ScheduleDirective::Kind::StreamLoads)
        .value("StreamStores", ScheduleDirective::Kind::StreamStores)
        .value("EagerInline", ScheduleDirective::Kind::EagerInline)
        .value("ComputeWith", ScheduleDirective::Kind::ComputeWith)
        .value("Prefetch", ScheduleDirective::Kind::Prefetch)
        .value("Specialize", ScheduleDirective::Kind::Specialize)
        .value("SpecializeFail", ScheduleDirective::Kind::SpecializeFail)
        .value("Rfactor", ScheduleDirective::Kind::Rfactor)
        .value("In", ScheduleDirective::Kind::In)
        .value("CloneIn", ScheduleDirective::Kind::CloneIn)
        .value("ComputeAt", ScheduleDirective::Kind::ComputeAt)
        .value("ComputeRoot", ScheduleDirective::Kind::ComputeRoot)
        .value("ComputeInline", ScheduleDirective::Kind::ComputeInline)
        .value("StoreAt", ScheduleDirective::Kind::StoreAt)
        .value("StoreRoot", ScheduleDirective::Kind::StoreRoot)
        .value("Bound", ScheduleDirective::Kind::Bound)
        .value("AlignStorage", ScheduleDirective::Kind::AlignStorage)
        .value("FoldStorage", ScheduleDirective::Kind::FoldStorage)
        .value("ReorderStorage", ScheduleDirective::Kind::ReorderStorage)
        .value("StoreIn", ScheduleDirective::Kind::StoreIn)
        .value("Memoize", ScheduleDirective::Kind::Memoize)
        .value("Async", ScheduleDirective::Kind::Async)
        .value("RingBuffer", ScheduleDirective::Kind::RingBuffer)
        .value("AlignBounds", ScheduleDirective::Kind::AlignBounds)
        .value("AlignExtent", ScheduleDirective::Kind::AlignExtent)
        .value("BoundExtent", ScheduleDirective::Kind::BoundExtent)
        .value("BoundStorage", ScheduleDirective::Kind::BoundStorage)
        .value("SetEstimate", ScheduleDirective::Kind::SetEstimate)
        .value("SetEstimates", ScheduleDirective::Kind::SetEstimates)
        .value("HoistStorage", ScheduleDirective::Kind::HoistStorage)
        .value("HoistStorageRoot", ScheduleDirective::Kind::HoistStorageRoot)
        .value("TraceLoads", ScheduleDirective::Kind::TraceLoads)
        .value("TraceStores", ScheduleDirective::Kind::TraceStores)
        .value("TraceRealizations", ScheduleDirective::Kind::TraceRealizations)
        .value("AddTraceTag", ScheduleDirective::Kind::AddTraceTag)
        .value("NoProfiling", ScheduleDirective::Kind::NoProfiling)
        .value("CopyToHost", ScheduleDirective::Kind::CopyToHost)
        .value("CopyToDevice", ScheduleDirective::Kind::CopyToDevice);

    directive.def(py::init<>())
        .def_readwrite("kind", &ScheduleDirective::kind)
        .def_readwrite("func", &ScheduleDirective::func)
        .def_readwrite("stage", &ScheduleDirective::stage)
        .def_readwrite("vars", &ScheduleDirective::vars)
        .def_readwrite("exprs", &ScheduleDirective::exprs)
        .def_readwrite("tail", &ScheduleDirective::tail)
        .def_readwrite("device", &ScheduleDirective::device)
        .def_readwrite("memory_type", &ScheduleDirective::memory_type)
        .def_readwrite("flag", &ScheduleDirective::flag)
        .def_readwrite("at_func", &ScheduleDirective::at_func)
        .def_readwrite("at_var", &ScheduleDirective::at_var)
        .def_readwrite("at_stage", &ScheduleDirective::at_stage)
        .def_readwrite("partition_policy", &ScheduleDirective::partition_policy)
        .def_readwrite("message", &ScheduleDirective::message)
        .def_readwrite("ref_funcs", &ScheduleDirective::ref_funcs)
        .def_readwrite("align", &ScheduleDirective::align)
        .def_readwrite("var_aligns", &ScheduleDirective::var_aligns)
        .def_readwrite("prefetch_strategy", &ScheduleDirective::prefetch_strategy)
        .def_readwrite("specialize_conditions", &ScheduleDirective::specialize_conditions)
        .def_readwrite("produces", &ScheduleDirective::produces)
        .def("to_source", &ScheduleDirective::to_source)
        .def("__repr__", [](const ScheduleDirective &d) {
            return "<halide.ScheduleDirective " + d.to_source() + ">";
        });

    // ---- StageHandle (fluent recorder) ----
    py::class_<StageHandle>(m, "StageHandle")
        .def("split", [](StageHandle &s, VarSpec old, VarSpec outer, VarSpec inner, Expr factor, TailStrategy tail) -> StageHandle { return chain(s, [&](StageHandle &h) { h.split(old, outer, inner, factor, tail); }); }, py::arg("old"), py::arg("outer"), py::arg("inner"), py::arg("factor"), py::arg("tail") = TailStrategy::Auto)
        .def("fuse", [](StageHandle &s, VarSpec inner, VarSpec outer, VarSpec fused) -> StageHandle { return chain(s, [&](StageHandle &h) { h.fuse(inner, outer, fused); }); }, py::arg("inner"), py::arg("outer"), py::arg("fused"))
        .def("rename", [](StageHandle &s, VarSpec old, VarSpec renamed) -> StageHandle { return chain(s, [&](StageHandle &h) { h.rename(old, renamed); }); }, py::arg("old"), py::arg("renamed"))
        .def("reorder", [](StageHandle &s, std::vector<VarSpec> vars) -> StageHandle { return chain(s, [&](StageHandle &h) { h.reorder(vars); }); }, py::arg("vars"))
        .def("tile", [](StageHandle &s, std::vector<VarSpec> previous, std::vector<VarSpec> outers, std::vector<VarSpec> inners, std::vector<Expr> factors, TailStrategy tail) -> StageHandle { return chain(s, [&](StageHandle &h) { h.tile(previous, outers, inners, factors, tail); }); }, py::arg("previous"), py::arg("outers"), py::arg("inners"), py::arg("factors"), py::arg("tail") = TailStrategy::Auto)
        .def("serial", [](StageHandle &s, VarSpec var) -> StageHandle { return chain(s, [&](StageHandle &h) { h.serial(var); }); }, py::arg("var"))
        .def("parallel", [](StageHandle &s, VarSpec var, Expr task_size) -> StageHandle { return chain(s, [&](StageHandle &h) {
                                                                                              if (task_size.defined()) {
                                                                                                  h.parallel(var, task_size);
                                                                                              } else {
                                                                                                  h.parallel(var);
                                                                                              }
                                                                                          }); }, py::arg("var"), py::arg("task_size") = Expr())
        .def("vectorize", [](StageHandle &s, VarSpec var, Expr factor) -> StageHandle { return chain(s, [&](StageHandle &h) {
                                                                                            if (factor.defined()) {
                                                                                                h.vectorize(var, factor);
                                                                                            } else {
                                                                                                h.vectorize(var);
                                                                                            }
                                                                                        }); }, py::arg("var"), py::arg("factor") = Expr())
        .def("unroll", [](StageHandle &s, VarSpec var, Expr factor) -> StageHandle { return chain(s, [&](StageHandle &h) {
                                                                                         if (factor.defined()) {
                                                                                             h.unroll(var, factor);
                                                                                         } else {
                                                                                             h.unroll(var);
                                                                                         }
                                                                                     }); }, py::arg("var"), py::arg("factor") = Expr())
        .def("gpu_blocks", [](StageHandle &s, std::vector<VarSpec> vars, DeviceAPI device) -> StageHandle { return chain(s, [&](StageHandle &h) { h.gpu_blocks(vars, device); }); }, py::arg("block_vars"), py::arg("device") = DeviceAPI::Default_GPU)
        .def("gpu_threads", [](StageHandle &s, std::vector<VarSpec> vars, DeviceAPI device) -> StageHandle { return chain(s, [&](StageHandle &h) { h.gpu_threads(vars, device); }); }, py::arg("thread_vars"), py::arg("device") = DeviceAPI::Default_GPU)
        .def("gpu_tile", [](StageHandle &s, VarSpec x, VarSpec bx, VarSpec tx, Expr x_size, TailStrategy tail, DeviceAPI device) -> StageHandle { return chain(s, [&](StageHandle &h) { h.gpu_tile(x, bx, tx, x_size, tail, device); }); }, py::arg("x"), py::arg("bx"), py::arg("tx"), py::arg("x_size"), py::arg("tail") = TailStrategy::Auto, py::arg("device") = DeviceAPI::Default_GPU)
        .def("compute_at", [](StageHandle &s, const std::string &at_func, VarSpec at_var, int at_stage) -> StageHandle { return chain(s, [&](StageHandle &h) { h.compute_at(at_func, at_var, at_stage); }); }, py::arg("func"), py::arg("var"), py::arg("stage") = -1)
        .def("compute_root", [](StageHandle &s) -> StageHandle { return chain(s, [&](StageHandle &h) { h.compute_root(); }); })
        .def("compute_inline", [](StageHandle &s) -> StageHandle { return chain(s, [&](StageHandle &h) { h.compute_inline(); }); })
        .def("store_at", [](StageHandle &s, const std::string &at_func, VarSpec at_var, int at_stage) -> StageHandle { return chain(s, [&](StageHandle &h) { h.store_at(at_func, at_var, at_stage); }); }, py::arg("func"), py::arg("var"), py::arg("stage") = -1)
        .def("store_root", [](StageHandle &s) -> StageHandle { return chain(s, [&](StageHandle &h) { h.store_root(); }); })
        .def("bound", [](StageHandle &s, VarSpec var, Expr min, Expr extent) -> StageHandle { return chain(s, [&](StageHandle &h) { h.bound(var, min, extent); }); }, py::arg("var"), py::arg("min"), py::arg("extent"))
        .def("compute_with", [](StageHandle &s, const std::string &with_func, VarSpec with_var, LoopAlignStrategy align, int with_stage) -> StageHandle { return chain(s, [&](StageHandle &h) { h.compute_with(with_func, with_var, align, with_stage); }); }, py::arg("func"), py::arg("var"), py::arg("align") = LoopAlignStrategy::Auto, py::arg("stage") = 0)
        .def("prefetch", [](StageHandle &s, const std::string &prefetched, VarSpec at, VarSpec from, Expr offset, PrefetchBoundStrategy strategy) -> StageHandle { return chain(s, [&](StageHandle &h) { h.prefetch(prefetched, at, from, offset, strategy); }); }, py::arg("func"), py::arg("at"), py::arg("from_var"), py::arg("offset") = 1, py::arg("strategy") = PrefetchBoundStrategy::GuardWithIf)
        .def("specialize", [](StageHandle &s, Expr condition) { return s.specialize(condition); }, py::arg("condition"))
        .def("rfactor", [](StageHandle &s, std::vector<std::pair<VarSpec, VarSpec>> preserved, std::string produces) { return s.rfactor(preserved, produces); }, py::arg("preserved"), py::arg("produces"))
        .def("update", &StageHandle::update, py::arg("stage") = 0);

    // ---- ScheduleEditor ----
    py::class_<ScheduleEditor>(m, "ScheduleEditor")
        .def(py::init<>())
        .def(py::init<const std::vector<Func> &>(), py::arg("funcs"))
        .def(py::init<const Pipeline &>(), py::arg("pipeline"))
        .def(py::init<const ScheduleDirectives &>(), py::arg("directives"))
        .def("register_func", &ScheduleEditor::register_func, py::arg("func"))
        .def("append", &ScheduleEditor::append, py::arg("directive"))
        .def("insert", &ScheduleEditor::insert, py::arg("index"), py::arg("directive"))
        .def("remove", &ScheduleEditor::remove, py::arg("index"))
        .def("replace", &ScheduleEditor::replace, py::arg("index"), py::arg("directive"))
        .def("move", &ScheduleEditor::move, py::arg("from_index"), py::arg("to_index"))
        .def("clear", &ScheduleEditor::clear)
        .def("size", &ScheduleEditor::size)
        .def("__len__", &ScheduleEditor::size)
        .def("__getitem__", [](const ScheduleEditor &e, size_t i) { return e[i]; })
        .def("directives", &ScheduleEditor::directives)
        .def("find", [](const ScheduleEditor &e, const std::string &f) { return e.find(f); }, py::arg("func"))
        .def("find", [](const ScheduleEditor &e, const std::string &f, int s) { return e.find(f, s); }, py::arg("func"), py::arg("stage"))
        .def("schedule", &ScheduleEditor::schedule, py::arg("func"), py::arg("stage") = 0)
        .def("insert_at", &ScheduleEditor::insert_at, py::arg("index"), py::arg("func"), py::arg("stage") = 0)
        .def("apply", py::overload_cast<const std::vector<Func> &>(&ScheduleEditor::apply, py::const_), py::arg("funcs"))
        .def("apply", py::overload_cast<const Pipeline &>(&ScheduleEditor::apply, py::const_), py::arg("pipeline"))
        .def("apply", py::overload_cast<>(&ScheduleEditor::apply, py::const_))
        .def("materialize", &ScheduleEditor::materialize);

    // ---- ScheduleAnalyzer ----
    py::class_<ScheduleAnalyzer>(m, "ScheduleAnalyzer")
        .def(py::init<const Func &>(), py::arg("func"))
        .def(py::init<const std::vector<Func> &>(), py::arg("funcs"))
        .def(py::init<const Pipeline &>(), py::arg("pipeline"))
        .def(py::init<const ScheduleDirectives &>(), py::arg("directives"))
        .def("directives", &ScheduleAnalyzer::directives)
        .def("func_names", &ScheduleAnalyzer::func_names)
        .def("has_func", &ScheduleAnalyzer::has_func, py::arg("func"))
        .def("stage_count", &ScheduleAnalyzer::stage_count, py::arg("func"))
        .def("vars", &ScheduleAnalyzer::vars, py::arg("func"), py::arg("stage") = 0)
        .def("has_var", &ScheduleAnalyzer::has_var, py::arg("func"), py::arg("var"))
        .def("directive_indices_for",
             py::overload_cast<const std::string &>(&ScheduleAnalyzer::directive_indices_for, py::const_),
             py::arg("func"))
        .def("directive_indices_for",
             py::overload_cast<const std::string &, int>(&ScheduleAnalyzer::directive_indices_for, py::const_),
             py::arg("func"), py::arg("stage"))
        .def_static("base_name", &ScheduleAnalyzer::base_name, py::arg("name"))
        // pybind11 can't expose one name as both a static and an instance method,
        // so the directive-list serializers are bound as static-only (matching
        // from_json); use ScheduleAnalyzer.to_source(a.directives()) on an
        // instance.
        .def_static("to_source",
                    py::overload_cast<const ScheduleDirectives &>(&ScheduleAnalyzer::to_source),
                    py::arg("directives"))
        .def_static("to_json",
                    py::overload_cast<const ScheduleDirectives &>(&ScheduleAnalyzer::to_json),
                    py::arg("directives"))
        .def_static("from_json", &ScheduleAnalyzer::from_json, py::arg("json"));

    // ---- ScheduleValidator ----
    auto validator = py::class_<ScheduleValidator>(m, "ScheduleValidator");

    auto issue = py::class_<ScheduleValidator::Issue>(validator, "Issue");
    py::enum_<ScheduleValidator::Issue::Kind>(issue, "Kind")
        .value("MissingFunc", ScheduleValidator::Issue::Kind::MissingFunc)
        .value("MissingVar", ScheduleValidator::Issue::Kind::MissingVar)
        .value("InvalidStage", ScheduleValidator::Issue::Kind::InvalidStage)
        .value("FuncLevelOnUpdate", ScheduleValidator::Issue::Kind::FuncLevelOnUpdate)
        .value("Malformed", ScheduleValidator::Issue::Kind::Malformed)
        .value("BadFactor", ScheduleValidator::Issue::Kind::BadFactor)
        .value("DuplicateVar", ScheduleValidator::Issue::Kind::DuplicateVar)
        .value("VarCollision", ScheduleValidator::Issue::Kind::VarCollision);
    issue.def_readonly("kind", &ScheduleValidator::Issue::kind)
        .def_readonly("directive", &ScheduleValidator::Issue::directive)
        .def_readonly("func", &ScheduleValidator::Issue::func)
        .def_readonly("name", &ScheduleValidator::Issue::name)
        .def_readonly("message", &ScheduleValidator::Issue::message)
        .def("__repr__", [](const ScheduleValidator::Issue &i) {
            return "<halide.ScheduleValidator.Issue: " + i.message + ">";
        });

    validator
        .def(py::init<const ScheduleDirectives &, const std::vector<Func> &>(),
             py::arg("directives"), py::arg("funcs"))
        .def(py::init<const ScheduleDirectives &, const Pipeline &>(),
             py::arg("directives"), py::arg("pipeline"))
        .def("is_valid", &ScheduleValidator::is_valid)
        .def("__bool__", &ScheduleValidator::is_valid)
        .def("issues", &ScheduleValidator::issues)
        .def("issues_for", &ScheduleValidator::issues_for, py::arg("index"))
        .def("missing_funcs", &ScheduleValidator::missing_funcs)
        .def("missing_vars", &ScheduleValidator::missing_vars)
        .def("report", &ScheduleValidator::report);
}

}  // namespace PythonBindings
}  // namespace Halide
