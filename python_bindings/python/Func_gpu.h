#ifndef FUNC_GPU_H
#define FUNC_GPU_H

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

/// Define all gpu related methods
void defineFuncGpuMethods(boost::python::class_<Halide::Func> &func_class);

namespace func_and_stage_implementation_details {
// These are methods shared with Stage

// we use hh and bp to avoid colisions with h, b used in the rest of the code
namespace hh = Halide;
namespace bp = boost::python;

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads0(FuncOrStage &that, hh::VarOrRVar thread_x, hh::DeviceAPI device_api) {
    return that.gpu_threads(thread_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads1(FuncOrStage &that, hh::VarOrRVar thread_x, hh::VarOrRVar thread_y, hh::DeviceAPI device_api) {
    return that.gpu_threads(thread_x, thread_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_threads2(FuncOrStage &that, hh::VarOrRVar thread_x, hh::VarOrRVar thread_y, hh::VarOrRVar thread_z, hh::DeviceAPI device_api) {
    return that.gpu_threads(thread_x, thread_y, thread_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks0(FuncOrStage &that, hh::VarOrRVar block_x, hh::DeviceAPI device_api) {
    return that.gpu_blocks(block_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks1(FuncOrStage &that, hh::VarOrRVar block_x, hh::VarOrRVar block_y, hh::DeviceAPI device_api) {
    return that.gpu_blocks(block_x, block_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_blocks2(FuncOrStage &that, hh::VarOrRVar block_x, hh::VarOrRVar block_y, hh::VarOrRVar block_z, hh::DeviceAPI device_api) {
    return that.gpu_blocks(block_x, block_y, block_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu0(FuncOrStage &that, hh::VarOrRVar block_x, hh::VarOrRVar thread_x,
                       hh::DeviceAPI device_api) {
    return that.gpu(block_x, thread_x, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu1(FuncOrStage &that, hh::VarOrRVar block_x, hh::VarOrRVar block_y,
                       hh::VarOrRVar thread_x, hh::VarOrRVar thread_y,
                       hh::DeviceAPI device_api) {
    return that.gpu(block_x, block_y, thread_x, thread_y, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu2(FuncOrStage &that, hh::VarOrRVar block_x, hh::VarOrRVar block_y, hh::VarOrRVar block_z,
                       hh::VarOrRVar thread_x, hh::VarOrRVar thread_y, hh::VarOrRVar thread_z,
                       hh::DeviceAPI device_api) {
    return that.gpu(block_x, block_y, block_z, thread_x, thread_y, thread_z, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile0(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar bx,
                            hh::Var tx, int x_size, hh::DeviceAPI device_api) {
    return that.gpu_tile(x, bx, tx, x_size, hh::TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile1(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar bx,
                            hh::RVar tx, int x_size, hh::DeviceAPI device_api) {
    return that.gpu_tile(x, bx, tx, x_size, hh::TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile2(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar tx,
                            int x_size, hh::DeviceAPI device_api) {
    return that.gpu_tile(x, tx, x_size, hh::TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile3(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                            hh::VarOrRVar bx, hh::VarOrRVar by,
                            hh::VarOrRVar tx, hh::VarOrRVar ty,
                            int x_size, int y_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, bx, by, tx, ty, x_size, y_size, hh::TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile4(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                            hh::VarOrRVar tx, hh::Var ty,
                            int x_size, int y_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, tx, ty, x_size, y_size, hh::TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile5(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                            hh::VarOrRVar tx, hh::RVar ty,
                            int x_size, int y_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, tx, ty, x_size, y_size, hh::TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile6(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y, hh::VarOrRVar z,
                            hh::VarOrRVar bx, hh::VarOrRVar by, hh::VarOrRVar bz,
                            hh::VarOrRVar tx, hh::VarOrRVar ty, hh::VarOrRVar tz,
                            int x_size, int y_size, int z_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, z, bx, by, bz, tx, ty, tz, x_size, y_size, z_size, hh::TailStrategy::Auto, device_api);
}

template <typename FuncOrStage>
FuncOrStage &func_gpu_tile7(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y, hh::VarOrRVar z,
                            hh::VarOrRVar tx, hh::VarOrRVar ty, hh::VarOrRVar tz,
                            int x_size, int y_size, int z_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, z, tx, ty, tx, x_size, y_size, z_size, hh::TailStrategy::Auto, device_api);
}

// Will be deprecated
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile8(FuncOrStage &that, hh::VarOrRVar x, int x_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, x_size, hh::TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile9(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y,
                            int x_size, int y_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, x_size, y_size, hh::TailStrategy::Auto, device_api);
}
template <typename FuncOrStage>
FuncOrStage &func_gpu_tile10(FuncOrStage &that, hh::VarOrRVar x, hh::VarOrRVar y, hh::VarOrRVar z,
                            int x_size, int y_size, int z_size,
                            hh::DeviceAPI device_api) {
    return that.gpu_tile(x, y, z, x_size, y_size, z_size, hh::TailStrategy::Auto, device_api);
}

/// Define all gpu related methods
template <typename FuncOrStage>
void defineFuncOrStageGpuMethods(bp::class_<FuncOrStage> &func_or_stage_class) {
    func_or_stage_class
        .def("gpu_threads", &func_gpu_threads2<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("thread_x"), bp::arg("thread_y"), bp::arg("thread_z"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "thread indices. This is useful if you compute a producer "
             "function within the block indices of a consumer function, and "
             "want to control how that function's dimensions map to GPU "
             "threads. If the selected target is not an appropriate GPU, this "
             "just marks those dimensions as parallel.")
        .def("gpu_threads", &func_gpu_threads1<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("thread_x"), bp::arg("thread_y"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_threads", &func_gpu_threads0<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("thread_x"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu_single_thread", &FuncOrStage::gpu_single_thread,
             (bp::arg("self"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>(),
             "Tell Halide to run this stage using a single gpu thread and "
             "block. This is not an efficient use of your GPU, but it can be "
             "useful to avoid copy-back for intermediate update stages that "
             "touch a very small part of your Func.");

    func_or_stage_class
        .def("gpu_blocks", &func_gpu_blocks2<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"), bp::arg("block_y"), bp::arg("block_z"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "block indices. This is useful for scheduling stages that will "
             "run serially within each GPU block. If the selected target is "
             "not ptx, this just marks those dimensions as parallel.")
        .def("gpu_blocks", &func_gpu_blocks1<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"), bp::arg("block_y"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_blocks", &func_gpu_blocks0<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu", &func_gpu2<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"), bp::arg("block_y"), bp::arg("block_z"),
              bp::arg("thread_x"), bp::arg("thread_y"), bp::arg("thread_z"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>(),
             "Tell Halide that the following dimensions correspond to GPU "
             "block indices and thread indices. If the selected target is not "
             "ptx, these just mark the given dimensions as parallel. The "
             "dimensions are consumed by this call, so do all other "
             "unrolling, reordering, etc first.")
        .def("gpu", &func_gpu1<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"), bp::arg("block_y"),
              bp::arg("thread_x"), bp::arg("thread_y"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu", &func_gpu0<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("block_x"), bp::arg("thread_x"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>());

    func_or_stage_class
        .def("gpu_tile", &func_gpu_tile0<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("bx"), bp::arg("tx"), bp::arg("x_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>(),
             "Short-hand for tiling a domain and mapping the tile indices "
             "to GPU block indices and the coordinates within each tile to "
             "GPU thread indices. Consumes the variables given, so do all "
             "other scheduling first.")
        .def("gpu_tile", &func_gpu_tile1<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("bx"), bp::arg("tx"), bp::arg("x_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile2<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("tx"), bp::arg("x_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile3<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"),
              bp::arg("bx"), bp::arg("by"),
              bp::arg("tx"), bp::arg("ty"),
              bp::arg("x_size"), bp::arg("y_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile4<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"),
              bp::arg("tx"), bp::arg("ty"),
              bp::arg("x_size"), bp::arg("y_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile5<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"),
              bp::arg("tx"), bp::arg("ty"),
              bp::arg("x_size"), bp::arg("y_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile6<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"), bp::arg("z"),
              bp::arg("bx"), bp::arg("by"), bp::arg("bz"),
              bp::arg("tx"), bp::arg("ty"), bp::arg("tz"),
              bp::arg("x_size"), bp::arg("y_size"), bp::arg("z_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile7<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"), bp::arg("z"),
              bp::arg("tx"), bp::arg("ty"), bp::arg("tz"),
              bp::arg("x_size"), bp::arg("y_size"), bp::arg("z_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())

        // Will be deprecated
        .def("gpu_tile", &func_gpu_tile8<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("x_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile9<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"),
              bp::arg("x_size"), bp::arg("y_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>())
        .def("gpu_tile", &func_gpu_tile10<FuncOrStage>,
             (bp::arg("self"),
              bp::arg("x"), bp::arg("y"), bp::arg("z"),
              bp::arg("x_size"), bp::arg("y_size"), bp::arg("z_size"),
              bp::arg("device_api") = hh::DeviceAPI::Default_GPU),
             bp::return_internal_reference<1>());

    return;
}

}  // end of namespace func_and_stage_implementation_details

#endif  // FUNC_GPU_H
