#ifndef HALIDE_PYTHON_BINDINGS_PYSCHEDULEMETHODS_H
#define HALIDE_PYTHON_BINDINGS_PYSCHEDULEMETHODS_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

// Methods that are defined on both Func and Stage.
template <typename PythonClass>
HALIDE_NEVER_INLINE void add_schedule_methods(PythonClass &class_instance) {
    using T = typename PythonClass::type;

    class_instance

    .def("compute_with", (T &(T::*)(Stage, VarOrRVar, const std::vector<std::pair<VarOrRVar, LoopAlignStrategy>> &)) &T::compute_with,
        py::arg("stage"), py::arg("var"), py::arg("align"))
    .def("compute_with", (T &(T::*)(Stage, VarOrRVar, LoopAlignStrategy)) &T::compute_with,
        py::arg("stage"), py::arg("var"), py::arg("align") = LoopAlignStrategy::Auto)

    .def("unroll", (T &(T::*)(VarOrRVar)) &T::unroll,
        py::arg("var"))
    .def("unroll", (T &(T::*)(VarOrRVar, Expr, TailStrategy)) &T::unroll,
        py::arg("var"), py::arg("factor"), py::arg("tail") = TailStrategy::Auto)

    .def("split", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, Expr, TailStrategy)) &T::split,
        py::arg("old"), py::arg("outer"), py::arg("inner"), py::arg("factor"), py::arg("tail") = TailStrategy::Auto)

    .def("fuse", &T::fuse,
        py::arg("inner"), py::arg("outer"), py::arg("fused"))

    .def("serial", &T::serial,
        py::arg("var"))

    .def("tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, TailStrategy)) &T::tile,
        py::arg("x"), py::arg("y"), py::arg("xo"), py::arg("yo"), py::arg("xi"), py::arg("yi"), py::arg("xfactor"), py::arg("yfactor"), py::arg("tail") = TailStrategy::Auto)
    .def("tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, TailStrategy)) &T::tile,
        py::arg("x"), py::arg("y"), py::arg("xi"), py::arg("yi"), py::arg("xfactor"), py::arg("yfactor"), py::arg("tail") = TailStrategy::Auto)

    .def("reorder", (T &(T::*)(const std::vector<VarOrRVar> &)) &T::reorder, py::arg("vars"))
    .def("reorder", [](T &t, py::args args) -> T & {
        return t.reorder(args_to_vector<VarOrRVar>(args));
    })

    .def("parallel", (T &(T::*)(VarOrRVar)) &T::parallel,
        py::arg("var"))
    .def("parallel", (T &(T::*)(VarOrRVar, Expr, TailStrategy)) &T::parallel,
        py::arg("var"), py::arg("task_size"), py::arg("tail") = TailStrategy::Auto)

    .def("vectorize", (T &(T::*)(VarOrRVar)) &T::vectorize,
        py::arg("var"))
    .def("vectorize", (T &(T::*)(VarOrRVar, Expr, TailStrategy)) &T::vectorize,
        py::arg("var"), py::arg("factor"), py::arg("tail") = TailStrategy::Auto)

    .def("gpu_blocks", (T &(T::*)(VarOrRVar, DeviceAPI)) &T::gpu_blocks,
        py::arg("block_x"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_blocks", (T &(T::*)(VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu_blocks,
        py::arg("block_x"), py::arg("block_y"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_blocks", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu_blocks,
        py::arg("block_x"), py::arg("block_y"), py::arg("block_z"), py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu", (T &(T::*)(VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu,
        py::arg("block_x"), py::arg("thread_x"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu,
        py::arg("block_x"), py::arg("block_y"), py::arg("thread_x"), py::arg("thread_y"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu,
        py::arg("block_x"), py::arg("block_y"), py::arg("block_z"), py::arg("thread_x"), py::arg("thread_y"), py::arg("thread_z"), py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_threads", (T &(T::*)(VarOrRVar, DeviceAPI)) &T::gpu_threads,
        py::arg("thread_x"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_threads", (T &(T::*)(VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu_threads,
        py::arg("thread_x"), py::arg("thread_y"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_threads", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, DeviceAPI)) &T::gpu_threads,
        py::arg("thread_x"), py::arg("thread_y"), py::arg("thread_z"), py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_single_thread", (T &(T::*)(DeviceAPI)) &T::gpu_single_thread,
        py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("bx"), py::arg("tx"), py::arg("x_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("tx"), py::arg("x_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("y"), py::arg("bx"), py::arg("by"), py::arg("tx"), py::arg("ty"), py::arg("x_size"), py::arg("y_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("y"), py::arg("tx"), py::arg("ty"), py::arg("x_size"), py::arg("y_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("y"), py::arg("z"), py::arg("bx"), py::arg("by"), py::arg("bz"), py::arg("tx"), py::arg("ty"), py::arg("tz"), py::arg("x_size"), py::arg("y_size"), py::arg("z_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)
    .def("gpu_tile", (T &(T::*)(VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, VarOrRVar, Expr, Expr, Expr, TailStrategy, DeviceAPI)) &T::gpu_tile,
        py::arg("x"), py::arg("y"), py::arg("z"), py::arg("tx"), py::arg("ty"), py::arg("tz"), py::arg("x_size"), py::arg("y_size"), py::arg("z_size"),
        py::arg("tail") = TailStrategy::Auto, py::arg("device_api") = DeviceAPI::Default_GPU)

    .def("rename", &T::rename,
        py::arg("old_name"), py::arg("new_name"))

    .def("specialize", &T::specialize,
        py::arg("condition"))
    .def("specialize_fail", &T::specialize_fail,
        py::arg("message"))

    .def("allow_race_conditions", &T::allow_race_conditions)

    .def("atomic", &T::atomic, py::arg("override_associativity_test") = false)

    .def("hexagon", &T::hexagon, py::arg("x") = Var::outermost())

    .def("prefetch", (T &(T::*)(const Func &, VarOrRVar, Expr, PrefetchBoundStrategy)) &T::prefetch,
        py::arg("func"), py::arg("var"), py::arg("offset") = 1, py::arg("strategy") = PrefetchBoundStrategy::GuardWithIf)
    .def("prefetch", [](T &t, const ImageParam &image, VarOrRVar var, Expr offset, PrefetchBoundStrategy strategy) -> T & {
        // Templated function; specializing only on ImageParam for now
        return t.prefetch(image, var, offset, strategy);
    }, py::arg("image"), py::arg("var"), py::arg("offset") = 1, py::arg("strategy") = PrefetchBoundStrategy::GuardWithIf)

    .def("source_location", &T::source_location)
    ;
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYSCHEDULEMETHODS_H
