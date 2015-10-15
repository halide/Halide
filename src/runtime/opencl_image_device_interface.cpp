#include "runtime_internal.h"
#include "device_interface.h"

extern "C" {
void halide_use_jit_module();
void halide_release_jit_module();
int halide_opencl_device_malloc(void*, buffer_t*);
int halide_opencl_device_free(void*, buffer_t*);
int halide_opencl_device_sync(void*, buffer_t*);
int halide_opencl_device_release(void*);
int halide_opencl_copy_to_host(void*, buffer_t*);
int halide_opencl_copy_to_device(void*, buffer_t*);
}

namespace Halide { namespace Runtime { namespace Internal { namespace OpenCL {

// Set opencl_device_interface to have its functions point to the
// versions of device_malloc, copy_to_host, and copy_to_device that
// will either use buffers or images depending on the demensions of
// the buffer.
WEAK halide_device_interface opencl_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_opencl_device_malloc,
    halide_opencl_device_free,
    halide_opencl_device_sync,
    halide_opencl_device_release,
    halide_opencl_copy_to_host,
    halide_opencl_copy_to_device,
};

}}}}
