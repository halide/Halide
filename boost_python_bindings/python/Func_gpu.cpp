#include "Func_gpu.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Func.h"

#include <boost/format.hpp>

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;


h::Func &func_gpu_threads0(h::Func &that, h::VarOrRVar thread_x, h::DeviceAPI device_api)
{
    return that.gpu_threads(thread_x, device_api);
}

h::Func &func_gpu_threads1(h::Func &that, h::VarOrRVar thread_x, h::VarOrRVar thread_y, h::DeviceAPI device_api)
{
    return that.gpu_threads(thread_x, thread_y, device_api);
}

h::Func &func_gpu_threads2(h::Func &that, h::VarOrRVar thread_x, h::VarOrRVar thread_y, h::VarOrRVar thread_z, h::DeviceAPI device_api)
{
    return that.gpu_threads(thread_x, thread_y, thread_z, device_api);
}


h::Func &func_gpu_blocks0(h::Func &that, h::VarOrRVar block_x, h::DeviceAPI device_api)
{
    return that.gpu_blocks(block_x, device_api);
}

h::Func &func_gpu_blocks1(h::Func &that, h::VarOrRVar block_x, h::VarOrRVar block_y, h::DeviceAPI device_api)
{
    return that.gpu_blocks(block_x, block_y, device_api);
}

h::Func &func_gpu_blocks2(h::Func &that, h::VarOrRVar block_x, h::VarOrRVar block_y, h::VarOrRVar block_z, h::DeviceAPI device_api)
{
    return that.gpu_blocks(block_x, block_y, block_z, device_api);
}


h::Func &func_gpu0(h::Func &that, h::VarOrRVar block_x, h::VarOrRVar thread_x,
                   h::DeviceAPI device_api)
{
    return that.gpu(block_x, thread_x, device_api);
}

h::Func &func_gpu1(h::Func &that, h::VarOrRVar block_x, h::VarOrRVar block_y,
                   h::VarOrRVar thread_x, h::VarOrRVar thread_y,
                   h::DeviceAPI device_api)
{
    return that.gpu(block_x, block_y, thread_x, thread_y, device_api);
}

h::Func &func_gpu2(h::Func &that, h::VarOrRVar block_x, h::VarOrRVar block_y, h::VarOrRVar block_z,
                   h::VarOrRVar thread_x, h::VarOrRVar thread_y,  h::VarOrRVar thread_z,
                   h::DeviceAPI device_api)
{
    return that.gpu(block_x, block_y, block_z, thread_x, thread_y, thread_z, device_api);
}



h::Func &func_gpu_tile0(h::Func &that, h::VarOrRVar x, int x_size, h::DeviceAPI device_api)
{
    return that.gpu_tile(x, x_size, device_api);
}

h::Func &func_gpu_tile1(h::Func &that, h::VarOrRVar x, h::VarOrRVar y,
                        int x_size, int y_size,
                        h::DeviceAPI device_api)
{
    return that.gpu_tile(x, y, x_size, y_size, device_api);
}

h::Func &func_gpu_tile2(h::Func &that, h::VarOrRVar x, h::VarOrRVar y, h::VarOrRVar z,
                        int x_size, int y_size, int z_size,
                        h::DeviceAPI device_api)
{
    return that.gpu_tile(x, y, z, x_size, y_size, z_size, device_api);
}


/// Define all gpu related methods
void defineFuncGpuMethods(p::class_<h::Func> &func_class)
{
    using Halide::Func;

    func_class
            .def("gpu_threads", &func_gpu_threads2, (p::arg("self"),
                                                     p::arg("thread_x"), p::arg("thread_y"), p::arg("thread_z"),
                                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>(),
                 "Tell Halide that the following dimensions correspond to GPU "
                 "thread indices. This is useful if you compute a producer "
                 "function within the block indices of a consumer function, and "
                 "want to control how that function's dimensions map to GPU "
                 "threads. If the selected target is not an appropriate GPU, this "
                 "just marks those dimensions as parallel.")
            .def("gpu_threads", &func_gpu_threads1, (p::arg("self"),
                                                     p::arg("thread_x"), p::arg("thread_y"),
                                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>())
            .def("gpu_threads", &func_gpu_threads0, (p::arg("self"),
                                                     p::arg("thread_x"),
                                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>());

    func_class
            .def("gpu_single_thread", &Func::gpu_single_thread, (p::arg("self"),
                                                                 p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>(),
                 "Tell Halide to run this stage using a single gpu thread and "
                 "block. This is not an efficient use of your GPU, but it can be "
                 "useful to avoid copy-back for intermediate update stages that "
                 "touch a very small part of your Func.");

    func_class
            .def("gpu_blocks", &func_gpu_blocks2, (p::arg("self"),
                                                   p::arg("block_x"), p::arg("block_y"), p::arg("block_z"),
                                                   p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>(),
                 "Tell Halide that the following dimensions correspond to GPU "
                 "block indices. This is useful for scheduling stages that will "
                 "run serially within each GPU block. If the selected target is "
                 "not ptx, this just marks those dimensions as parallel.")
            .def("gpu_blocks", &func_gpu_blocks1, (p::arg("self"),
                                                   p::arg("block_x"), p::arg("block_y"),
                                                   p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>())
            .def("gpu_blocks", &func_gpu_blocks0, (p::arg("self"),
                                                   p::arg("block_x"),
                                                   p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>());

    func_class
            .def("gpu", &func_gpu2, (p::arg("self"),
                                     p::arg("block_x"), p::arg("block_y"), p::arg("block_z"),
                                     p::arg("thread_x"), p::arg("thread_y"), p::arg("thread_z"),
                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>(),
                 "Tell Halide that the following dimensions correspond to GPU "
                 "block indices and thread indices. If the selected target is not "
                 "ptx, these just mark the given dimensions as parallel. The "
                 "dimensions are consumed by this call, so do all other "
                 "unrolling, reordering, etc first.")
            .def("gpu", &func_gpu1, (p::arg("self"),
                                     p::arg("block_x"), p::arg("block_y"),
                                     p::arg("thread_x"), p::arg("thread_y"),
                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>())
            .def("gpu", &func_gpu0, (p::arg("self"),
                                     p::arg("block_x"), p::arg("thread_x"),
                                     p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>());

    func_class
            .def("gpu_tile", &func_gpu_tile2, (p::arg("self"),
                                               p::arg("x"), p::arg("y"), p::arg("z"),
                                               p::arg("x_size"), p::arg("y_size"), p::arg("z_size"),
                                               p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>(),
                 "Short-hand for tiling a domain and mapping the tile indices "
                 "to GPU block indices and the coordinates within each tile to "
                 "GPU thread indices. Consumes the variables given, so do all "
                 "other scheduling first.")
            .def("gpu_tile", &func_gpu_tile1, (p::arg("self"),
                                               p::arg("x"), p::arg("y"),
                                               p::arg("x_size"), p::arg("y_size"),
                                               p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>())
            .def("gpu_tile", &func_gpu_tile0, (p::arg("self"),
                                               p::arg("x"), p::arg("x_size"),
                                               p::arg("device_api") = h::DeviceAPI::Default_GPU),
                 p::return_internal_reference<1>());

    return;
}


