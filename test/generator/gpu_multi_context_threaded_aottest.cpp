#include <stdio.h>

// This test demonstrates how to use more than one GPU context with
// Halide generated GPU support, specifically in a multithreaded
// program. It of course also tests that this works correctly with the
// Halide GPU runtimes.

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
#include <thread>

#include "gpu_context.h"

#include "gpu_multi_context_threaded_add.h"
#include "gpu_multi_context_threaded_mul.h"

using namespace Halide::Runtime;

const int W = 32, H = 32;

#if defined(TEST_OPENCL)

struct gpu_context {
    cl_context cl_ctx;
    cl_command_queue cl_q;
};

// Create the global context. This is just a helper function not called by Halide.
bool init_context(gpu_context &context) {
    return create_opencl_context(context.cl_ctx, context.cl_q);
}

void destroy_context(gpu_context &context) {
    destroy_opencl_context(context.cl_ctx, context.cl_q);
    context.cl_q = nullptr;
    context.cl_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/opencl.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q, bool create) {
    if (user_context == nullptr) {
        assert(!create);
        *ctx = nullptr;
        *q = nullptr;
    } else {
        const gpu_context *context = (const gpu_context *)user_context;
        *ctx = context->cl_ctx;
        *q = context->cl_q;
    }
    return 0;
}

extern "C" int halide_release_cl_context(void *user_context) {
    return 0;
}

#define HAS_MULTIPLE_CONTEXTS true
#elif defined(TEST_CUDA)

typedef CUcontext gpu_context;

bool init_context(gpu_context &cuda_ctx) {
    return create_cuda_context(cuda_ctx);
}

void destroy_context(gpu_context &cuda_ctx) {
    destroy_cuda_context(cuda_ctx);
    cuda_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/cuda.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create) {
    if (user_context == nullptr) {
        assert(!create);
        *ctx = nullptr;
    } else {
        *ctx = *(CUcontext *)user_context;
    }
    return 0;
}

extern "C" int halide_cuda_release_context(void *user_context) {
    return 0;
}

#define HAS_MULTIPLE_CONTEXTS true
#elif defined(TEST_METAL) && defined(__OBJC__)

struct gpu_context {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
};

bool init_context(gpu_context &context) {
    create_metal_context(context.device, context.queue);
    return 0;
}

void destroy_context(gpu_context &context) {
    destroy_metal_context(context.device, context.queue);
    context.device = nullptr;
    context.queue = nullptr;
}

int halide_metal_acquire_context(void *user_context, id<MTLDevice> *device_ret,
                                 id<MTLCommandQueue> *queue_ret, bool create) {
    if (user_context == nullptr) {
        assert(!create);
        *device_ret = nullptr;
        *queue_ret = nullptr;
    } else {
        gpu_context *context = (gpu_context *)user_context;
        *device_ret = context->device;
        *queue_ret = context->queue;
    }
    return 0;
}

int halide_metal_release_context(void *user_context) {
    return 0;
}

#define HAS_MULTIPLE_CONTEXTS true
#elif defined(TEST_WEBGPU)

struct gpu_context {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUBuffer staging_buffer = nullptr;
};

bool init_context(gpu_context &ctx) {
    return create_webgpu_context(&ctx.instance, &ctx.adapter, &ctx.device, &ctx.staging_buffer);
}

void destroy_context(gpu_context &ctx) {
    destroy_webgpu_context(ctx.instance, ctx.adapter, ctx.device, ctx.staging_buffer);
    ctx.instance = nullptr;
    ctx.adapter = nullptr;
    ctx.device = nullptr;
    ctx.staging_buffer = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/webgpu.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_webgpu_acquire_context(void *user_context,
                                             WGPUInstance *instance_ret,
                                             WGPUAdapter *adapter_ret,
                                             WGPUDevice *device_ret,
                                             WGPUBuffer *staging_buffer_ret,
                                             bool create) {
    if (user_context == nullptr) {
        assert(!create);
        *instance_ret = nullptr;
        *adapter_ret = nullptr;
        *device_ret = nullptr;
        *staging_buffer_ret = nullptr;
        return -1;
    } else {
        gpu_context *context = (gpu_context *)user_context;
        *instance_ret = context->instance;
        *adapter_ret = context->adapter;
        *device_ret = context->device;
        *staging_buffer_ret = context->staging_buffer;
    }
    return 0;
}

extern "C" int halide_webgpu_release_context(void *user_context) {
    return 0;
}

#define HAS_MULTIPLE_CONTEXTS true
#else
typedef int gpu_context;

// Just use the default implementation of acquire/release.
bool init_context(int &context) {
    printf("Using default implementation of acquire/release\n");
    context = 0;
    return true;
}
void destroy_context(int &context) {
    context = 0;
}

#define HAS_MULTIPLE_CONTEXTS false
#endif

void run_kernels_on_thread(gpu_context context1, bool destroy_when_done) {
    gpu_context context2;

    Buffer<int32_t, 2> buf1_in(W, H);
    Buffer<int32_t, 2> buf1_result(W, H);
    buf1_in.fill(0);

    const halide_device_interface_t *device_interface;

    int val = 0;
    for (int i = 0; i < 10; i++) {
        init_context(context2);

        Buffer<int32_t, 2> buf2_in(W, H);
        Buffer<int32_t, 2> buf2_result(W, H);
        buf2_in.fill(0);

        gpu_multi_context_threaded_add(&context1, buf1_in, buf1_result);
        gpu_multi_context_threaded_mul(&context1, buf1_result, buf1_in);
        gpu_multi_context_threaded_add(&context1, buf1_in, buf1_result);

        gpu_multi_context_threaded_add(&context2, buf2_in, buf2_result);
        gpu_multi_context_threaded_mul(&context2, buf2_result, buf2_in);
        gpu_multi_context_threaded_add(&context2, buf2_in, buf2_result);

        buf1_result.copy_to_host(&context1);
        buf2_result.copy_to_host(&context2);

        val += 2;
        val *= 2;
        assert(buf1_result.all_equal(val + 2));
        assert(buf2_result.all_equal(6));

        device_interface = buf1_result.raw_buffer()->device_interface;

        // About to destroy context, so ensure allocations are freed first.
        buf2_in.device_free(&context2);
        buf2_result.device_free(&context2);

        if (device_interface != nullptr) {
            halide_device_release(&context2, device_interface);
        }
        destroy_context(context2);
    }

    // About to destroy context, so ensure allocations are freed first.
    buf1_in.device_free(&context1);
    buf1_result.device_free(&context1);

    if (destroy_when_done && device_interface != nullptr) {
        halide_device_release(&context1, device_interface);
        destroy_context(context1);
    }
}

int main(int argc, char **argv) {
    gpu_context contexta;
    init_context(contexta);

    gpu_context contextb;
    init_context(contextb);

    std::thread thread1(run_kernels_on_thread, contexta, false);
    std::thread thread2(run_kernels_on_thread, contextb, false);

    thread1.join();
    thread2.join();

    // Make sure using the same context on different threads works.
    std::thread thread3(run_kernels_on_thread, contexta, HAS_MULTIPLE_CONTEXTS);
    std::thread thread4(run_kernels_on_thread, contextb, HAS_MULTIPLE_CONTEXTS);

    thread3.join();
    thread4.join();

    printf("Success!\n");
    return 0;
}
#endif  // !WIN32
