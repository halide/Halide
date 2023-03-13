#ifndef GPU_OBJECT_LIFETIME_H
#define GPU_OBJECT_LIFETIME_H

#include <array>
#include <stdio.h>
#include <string.h>

namespace Halide {
namespace Internal {

class GpuObjectLifetimeTracker {
    struct ObjectType {
        const char *const created;
        const char *const destroyed;
        bool const is_global;
        int total_created;
        int live_count;

        ObjectType(const char *created, const char *destroyed, bool is_global = false)
            : created(created), destroyed(destroyed),
              is_global(is_global), total_created(0), live_count(0) {
        }
    };

    std::array<ObjectType, 14> object_types = {{
        {"Caching compiled kernel:", "Releasing cached compilation:"},

        // OpenCL objects
        {"clCreateContext", "clReleaseContext", true},
        {"clCreateCommandQueue", "clReleaseCommandQueue", true},
        // This handles both "clCreateProgramWithSource" and
        // "clCreateProgramWithBinary".
        {"clCreateBuffer", "clReleaseMemObject"},
        {"clCreateKernel", "clReleaseKernel"},

        // CUDA objects
        {"cuCtxCreate", "cuCtxDestroy", true},
        {"cuMemAlloc", "cuMemFree"},

        // Metal objects
        {"Allocating: MTLCreateSystemDefaultDevice", "Releasing: MTLCreateSystemDefaultDevice", true},
        {"Allocating: new_command_queue", "Releasing: new_command_queue"},

        // Hexagon objects
        {"halide_remote_load_library", "halide_remote_release_library"},
        {"ion_alloc", "ion_free"},

        // WebGPU objects
        {"wgpuCreateInstance", "wgpuInstanceRelease", true},
        {"wgpuDeviceCreateBuffer", "wgpuBufferRelease"},
        {"wgpuDeviceCreateComputePipeline", "wgpuComputePipelineRelease"},
    }};

public:
    // Parse a line of output from gpu_debug and update object counts.
    void record_gpu_debug(const char *str) {
        for (auto &o : object_types) {
            if (strstr(str, o.created)) {
                o.total_created++;
                o.live_count++;
            } else if (strstr(str, o.destroyed)) {
                o.live_count--;
            }
        }
    }

    // Check that there are no live objects remaining, and we created at least one object.
    int validate_gpu_object_lifetime(bool allow_globals, bool allow_none, int max_globals) {
        int total = 0;
        for (auto &o : object_types) {
            if (o.live_count != 0 &&
                !(allow_globals && o.is_global)) {
                printf("Error! %d objects created by %s still live\n",
                       o.live_count, o.created);
                return 1;
            }
            if (o.is_global && o.total_created > max_globals) {
                printf("Error! %d global objects created by %s, max is %d\n",
                       o.total_created, o.created, max_globals);
                return 1;
            }

            total += o.total_created;
        }
        if (!allow_none && total == 0) {
            printf("Error! No objects created. Ensure gpu_debug is set, ");
            printf("and record_gpu_debug is called from halide_print.\n");
            return 1;
        }
        return 0;
    }
};

}  // namespace Internal
}  // namespace Halide

#endif
