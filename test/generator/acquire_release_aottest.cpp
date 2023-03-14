#include <stdio.h>

#ifdef _WIN32
int main(int argc, char **argv) {
    printf("[SKIP] Test requires weak linkage, which is not available on Windows.\n");
    return 0;
}
#else

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <string.h>

#include "acquire_release.h"
#include "gpu_context.h"

using namespace Halide::Runtime;

const int W = 256, H = 256;

#if defined(TEST_OPENCL)

// Just use a global context and queue, created and destroyed by main.
cl_context cl_ctx = nullptr;
cl_command_queue cl_q = nullptr;

// Create the global context. This is just a helper function not called by Halide.
bool init_context() {
    return create_opencl_context(cl_ctx, cl_q);
}

void destroy_context() {
    destroy_opencl_context(cl_ctx, cl_q);
    cl_q = nullptr;
    cl_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/opencl.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    printf("Acquired CL context %p\n", cl_ctx);
    *ctx = cl_ctx;
    *q = cl_q;
    return 0;
}

extern "C" int halide_release_cl_context(void *user_context) {
    printf("Releasing CL context %p\n", cl_ctx);
    return 0;
}
#elif defined(TEST_CUDA)
CUcontext cuda_ctx = nullptr;

bool init_context() {
    return create_cuda_context(cuda_ctx);
}

void destroy_context() {
    destroy_cuda_context(cuda_ctx);
    cuda_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/cuda.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
    printf("Acquired CUDA context %p\n", cuda_ctx);
    *ctx = cuda_ctx;
    return 0;
}

extern "C" int halide_cuda_release_context(void *user_context) {
    printf("Releasing CUDA context %p\n", cuda_ctx);
    return 0;
}
#elif defined(TEST_METAL) && defined(__OBJC__)

struct gpu_context {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
} metal_context;

bool init_context() {
    return create_metal_context(metal_context.device, metal_context.queue);
}

void destroy_context() {
    destroy_metal_context(metal_context.device, metal_context.queue);
    metal_context.device = nullptr;
    metal_context.queue = nullptr;
}

int halide_metal_acquire_context(void *user_context, id<MTLDevice> *device_ret,
                                 id<MTLCommandQueue> *queue_ret, bool create) {
    *device_ret = metal_context.device;
    *queue_ret = metal_context.queue;

    return 0;
}

int halide_metal_release_context(void *user_context) {
    return 0;
}
#elif defined(TEST_WEBGPU)

struct gpu_context {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUBuffer staging_buffer = nullptr;
} webgpu_context;

bool init_context() {
    return create_webgpu_context(&webgpu_context.instance, &webgpu_context.adapter, &webgpu_context.device, &webgpu_context.staging_buffer);
}

void destroy_context() {
    destroy_webgpu_context(webgpu_context.instance, webgpu_context.adapter, webgpu_context.device, webgpu_context.staging_buffer);
    webgpu_context.instance = nullptr;
    webgpu_context.adapter = nullptr;
    webgpu_context.device = nullptr;
    webgpu_context.staging_buffer = nullptr;
}

extern "C" int halide_webgpu_acquire_context(void *user_context,
                                             WGPUInstance *instance_ret,
                                             WGPUAdapter *adapter_ret,
                                             WGPUDevice *device_ret,
                                             WGPUBuffer *staging_buffer_ret,
                                             bool create) {
    *instance_ret = webgpu_context.instance;
    *adapter_ret = webgpu_context.adapter;
    *device_ret = webgpu_context.device;
    *staging_buffer_ret = webgpu_context.staging_buffer;
    return 0;
}

extern "C" int halide_webgpu_release_context(void *user_context) {
    return 0;
}

#define HAS_MULTIPLE_CONTEXTS true
#else
// Just use the default implementation of acquire/release.
bool init_context() {
    printf("Using default implementation of acquire/release\n");
    return true;
}
void destroy_context() {
}
#endif

bool run_test() {
    // Initialize the runtime specific GPU context.
    if (!init_context()) {
        return false;
    }

    // Everything else is a normal Halide program. The GPU runtime will call
    // the above acquire/release functions to get the context instead of using
    // its own internal context.
    Buffer<float, 2> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(x * y);
        }
    }

    input.set_host_dirty(true);

    Buffer<float, 2> output(W, H);

    acquire_release(input, output);

    output.copy_to_host();

    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (input(x, y) * 2.0f + 1.0f != output(x, y)) {
                printf("Error at (%d, %d): %f != %f\n", x, y, input(x, y) * 2.0f + 1.0f,
                       output(x, y));
                return false;
            }
        }
    }

    const halide_device_interface_t *interface = output.raw_buffer()->device_interface;

    // We need to free our GPU buffers before destroying the context.
    input.device_free();
    output.device_free();

    if (interface != nullptr) {
        halide_device_release(nullptr, interface);

        // Free the context we created.
        destroy_context();
    } else {
        printf("Device interface is nullptr.\n");
    }

    printf("Success!\n");
    return true;
}

int main(int argc, char **argv) {
    if (!run_test()) {
        return 1;
    }

    if (!run_test()) {
        return 1;
    }

    return 0;
}

#endif
