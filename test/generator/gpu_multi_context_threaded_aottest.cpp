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
    cl_q = nullptr;
    cl_ctx = nullptr;
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
#elif defined(TEST_CUDA)

typedef CUcontext gpu_context;

bool init_context(CUcontext &cuda_ctx) {
    return create_cuda_context(cuda_ctx);
}

void destroy_context(CUcontext &cuda_ctx) {
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
#else
// Just use the default implementation of acquire/release.
bool init_context() {
    printf("Using default implementation of acquire/release\n");
    return true;
}
void destroy_context() {
}
#endif

void run_kernels_on_thread(gpu_context context1, bool destroy_when_done) {
    gpu_context context2;

    Buffer<int32_t> buf1_in(W, H);
    Buffer<int32_t> buf1_result(W, H);
    buf1_in.fill(0);

    const halide_device_interface_t *device_interface;

    int val = 0;
    for (int i = 0; i < 10; i++) {
        init_context(context2);

        Buffer<int32_t> buf2_in(W, H);
        Buffer<int32_t> buf2_result(W, H);
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

        halide_device_release(&context2, device_interface);
        destroy_context(context2);
    }

    // About to destroy context, so ensure allocations are freed first.
    buf1_in.device_free(&context1);
    buf1_result.device_free(&context1);

    if (destroy_when_done) {
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
    std::thread thread3(run_kernels_on_thread, contexta, true);
    std::thread thread4(run_kernels_on_thread, contextb, true);

    thread3.join();
    thread4.join();

    printf("Success!\n");
    return 0;
}
#endif  // !WIN32
