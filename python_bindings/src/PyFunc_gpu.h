#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_GPU_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_GPU_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

/// Define all gpu related methods
void define_func_gpu_methods(py::class_<Func> &func_class);

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads0(FuncOrStage &that, VarOrRVar thread_x, DeviceAPI device_api) {
    return that.gpu_threads(thread_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads1(FuncOrStage &that, VarOrRVar thread_x, VarOrRVar thread_y, DeviceAPI device_api) {
    return that.gpu_threads(thread_x, thread_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads2(FuncOrStage &that, VarOrRVar thread_x, VarOrRVar thread_y, VarOrRVar thread_z, DeviceAPI device_api) {
    return that.gpu_threads(thread_x, thread_y, thread_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks0(FuncOrStage &that, VarOrRVar block_x, DeviceAPI device_api) {
    return that.gpu_blocks(block_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks1(FuncOrStage &that, VarOrRVar block_x, VarOrRVar block_y, DeviceAPI device_api) {
    return that.gpu_blocks(block_x, block_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks2(FuncOrStage &that, VarOrRVar block_x, VarOrRVar block_y, VarOrRVar block_z, DeviceAPI device_api) {
    return that.gpu_blocks(block_x, block_y, block_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu0(FuncOrStage &that, VarOrRVar block_x, VarOrRVar thread_x,
                       DeviceAPI device_api) {
    return that.gpu(block_x, thread_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu1(FuncOrStage &that, VarOrRVar block_x, VarOrRVar block_y,
                       VarOrRVar thread_x, VarOrRVar thread_y,
                       DeviceAPI device_api) {
    return that.gpu(block_x, block_y, thread_x, thread_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu2(FuncOrStage &that, VarOrRVar block_x, VarOrRVar block_y, VarOrRVar block_z,
                       VarOrRVar thread_x, VarOrRVar thread_y, VarOrRVar thread_z,
                       DeviceAPI device_api) {
    return that.gpu(block_x, block_y, block_z, thread_x, thread_y, thread_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile0(FuncOrStage &that, VarOrRVar x, VarOrRVar bx,
                            Var tx, int x_size, DeviceAPI device_api) {
    return that.gpu_tile(x, bx, tx, x_size, TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile1(FuncOrStage &that, VarOrRVar x, VarOrRVar bx,
                            RVar tx, int x_size, DeviceAPI device_api) {
    return that.gpu_tile(x, bx, tx, x_size, TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile2(FuncOrStage &that, VarOrRVar x, VarOrRVar tx,
                            int x_size, DeviceAPI device_api) {
    return that.gpu_tile(x, tx, x_size, TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile3(FuncOrStage &that, VarOrRVar x, VarOrRVar y,
                            VarOrRVar bx, VarOrRVar by,
                            VarOrRVar tx, VarOrRVar ty,
                            int x_size, int y_size,
                            DeviceAPI device_api) {
    return that.gpu_tile(x, y, bx, by, tx, ty, x_size, y_size, TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile4(FuncOrStage &that, VarOrRVar x, VarOrRVar y,
                            VarOrRVar tx, Var ty,
                            int x_size, int y_size,
                            DeviceAPI device_api) {
    return that.gpu_tile(x, y, tx, ty, x_size, y_size, TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile5(FuncOrStage &that, VarOrRVar x, VarOrRVar y,
                            VarOrRVar tx, RVar ty,
                            int x_size, int y_size,
                            DeviceAPI device_api) {
    return that.gpu_tile(x, y, tx, ty, x_size, y_size, TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile6(FuncOrStage &that, VarOrRVar x, VarOrRVar y, VarOrRVar z,
                            VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                            VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                            int x_size, int y_size, int z_size,
                            DeviceAPI device_api) {
    return that.gpu_tile(x, y, z, bx, by, bz, tx, ty, tz, x_size, y_size, z_size, TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile7(FuncOrStage &that, VarOrRVar x, VarOrRVar y, VarOrRVar z,
                            VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                            int x_size, int y_size, int z_size,
                            DeviceAPI device_api) {
    return that.gpu_tile(x, y, z, tx, ty, tx, x_size, y_size, z_size, TailStrategy::Auto, device_api);
}

/// Define all gpu related methods
template <typename FuncOrStage>
void define_func_or_stage_gpu_methods(py::class_<FuncOrStage> &func_or_stage_class) {
    func_or_stage_class
        .def("gpu_threads", &func_gpu_threads2<FuncOrStage>,
             (py::arg("self"),
              py::arg("thread_x"), py::arg("thread_y"), py::arg("thread_z"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "thread indices. This is useful if you compute a producer "
             "function within the block indices of a consumer function, and "
             "want to control how that function's dimensions map to GPU "
             "threads. If the selected target is not an appropriate GPU, this "
             "just marks those dimensions as parallel.")
        .def("gpu_threads", &func_gpu_threads1<FuncOrStage>,
             (py::arg("self"),
              py::arg("thread_x"), py::arg("thread_y"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_threads", &func_gpu_threads0<FuncOrStage>,
             (py::arg("self"),
              py::arg("thread_x"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu_single_thread", &FuncOrStage::gpu_single_thread,
             (py::arg("self"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>(),
             "Tell Halide to run this stage using a single gpu thread and "
             "block. This is not an efficient use of your GPU, but it can be "
             "useful to avoid copy-back for intermediate update stages that "
             "touch a very small part of your Func.");

    func_or_stage_class
        .def("gpu_blocks", &func_gpu_blocks2<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"), py::arg("block_y"), py::arg("block_z"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "block indices. This is useful for scheduling stages that will "
             "run serially within each GPU block. If the selected target is "
             "not ptx, this just marks those dimensions as parallel.")
        .def("gpu_blocks", &func_gpu_blocks1<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"), py::arg("block_y"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_blocks", &func_gpu_blocks0<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu", &func_gpu2<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"), py::arg("block_y"), py::arg("block_z"),
              py::arg("thread_x"), py::arg("thread_y"), py::arg("thread_z"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "block indices and thread indices. If the selected target is not "
             "ptx, these just mark the given dimensions as parallel. The "
             "dimensions are consumed by this call, so do all other "
             "unrolling, reordering, etc first.")
        .def("gpu", &func_gpu1<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"), py::arg("block_y"),
              py::arg("thread_x"), py::arg("thread_y"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu", &func_gpu0<FuncOrStage>,
             (py::arg("self"),
              py::arg("block_x"), py::arg("thread_x"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu_tile", &func_gpu_tile0<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("bx"), py::arg("tx"), py::arg("x_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>(),
             "Short-hand for tiling a domain and mapping the tile indices "
             "to GPU block indices and the coordinates within each tile to "
             "GPU thread indices. Consumes the variables given, so do all "
             "other scheduling first.")
        .def("gpu_tile", &func_gpu_tile1<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("bx"), py::arg("tx"), py::arg("x_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile2<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("tx"), py::arg("x_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile3<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("y"),
              py::arg("bx"), py::arg("by"),
              py::arg("tx"), py::arg("ty"),
              py::arg("x_size"), py::arg("y_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile4<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("y"),
              py::arg("tx"), py::arg("ty"),
              py::arg("x_size"), py::arg("y_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile5<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("y"),
              py::arg("tx"), py::arg("ty"),
              py::arg("x_size"), py::arg("y_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile6<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("y"), py::arg("z"),
              py::arg("bx"), py::arg("by"), py::arg("bz"),
              py::arg("tx"), py::arg("ty"), py::arg("tz"),
              py::arg("x_size"), py::arg("y_size"), py::arg("z_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile7<FuncOrStage>,
             (py::arg("self"),
              py::arg("x"), py::arg("y"), py::arg("z"),
              py::arg("tx"), py::arg("ty"), py::arg("tz"),
              py::arg("x_size"), py::arg("y_size"), py::arg("z_size"),
              py::arg("device_api") = DeviceAPI::Default_GPU),
             py::return_internal_reference<1>());
}


}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_GPU_H
