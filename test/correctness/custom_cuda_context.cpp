#include "Halide.h"

#ifndef __linux__

int main(int argc, char **argv) {
    printf("[SKIP] Test only runs on linux.\n");
    return 0;
}

#else

#include <dlfcn.h>

using namespace Halide;

void *lib_cuda = nullptr;
int (*cuStreamCreate)(void **, uint32_t) = nullptr;
int (*cuCtxCreate)(void **, uint32_t, int) = nullptr;
int (*cuCtxDestroy)(void *) = nullptr;
int (*cuMemAlloc)(void **, size_t) = nullptr;
int (*cuMemFree)(void *) = nullptr;
int (*cuCtxSetCurrent)(void *) = nullptr;

struct CudaState : public Halide::JITUserContext {
    void *cuda_context = nullptr, *cuda_stream = nullptr;
    std::atomic<int> acquires = 0, releases = 0;

    static int my_cuda_acquire_context(JITUserContext *ctx, void **cuda_ctx, bool create) {
        CudaState *state = (CudaState *)ctx;
        *cuda_ctx = state->cuda_context;
        state->acquires++;
        return 0;
    }

    static int my_cuda_release_context(JITUserContext *ctx) {
        CudaState *state = (CudaState *)ctx;
        state->releases++;
        return 0;
    }

    static int my_cuda_get_stream(JITUserContext *ctx, void *cuda_ctx, void **stream) {
        CudaState *state = (CudaState *)ctx;
        *stream = state->cuda_stream;
        return 0;
    }

    CudaState() {
        handlers.custom_cuda_acquire_context = CudaState::my_cuda_acquire_context;
        handlers.custom_cuda_release_context = CudaState::my_cuda_release_context;
        handlers.custom_cuda_get_stream = CudaState::my_cuda_get_stream;
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] CUDA not enabled.\n");
        return 0;
    }

    {
        // Do some nonsense to load libcuda without having to the CUDA
        // sdk. This would not be necessary in a real application.

        // Trick the runtime into loading libcuda
        Func f;
        Var x;
        f(x) = x;
        f.gpu_single_thread();
        f.realize({8});

        // Get a handle to it
        lib_cuda = dlopen("libcuda.so", RTLD_LAZY | RTLD_GLOBAL | RTLD_NOLOAD);

        if (!lib_cuda) {
            printf("[SKIP] Failed to load libcuda\n");
            return 0;
        }

        cuStreamCreate = (decltype(cuStreamCreate))dlsym(lib_cuda, "cuStreamCreate");
        cuCtxCreate = (decltype(cuCtxCreate))dlsym(lib_cuda, "cuCtxCreate_v2");
        cuCtxDestroy = (decltype(cuCtxDestroy))dlsym(lib_cuda, "cuCtxDestroy_v2");
        cuCtxSetCurrent = (decltype(cuCtxSetCurrent))dlsym(lib_cuda, "cuCtxSetCurrent");
        cuMemAlloc = (decltype(cuMemAlloc))dlsym(lib_cuda, "cuMemAlloc_v2");
        cuMemFree = (decltype(cuMemFree))dlsym(lib_cuda, "cuMemFree_v2");

        if (cuStreamCreate == nullptr ||
            cuCtxCreate == nullptr ||
            cuCtxDestroy == nullptr ||
            cuCtxSetCurrent == nullptr ||
            cuMemAlloc == nullptr ||
            cuMemFree == nullptr) {
            printf("Failed to find cuda API\n");
            return -1;
        }
    }

    CudaState state;
    int err = cuCtxCreate(&state.cuda_context, 0, 0);
    if (state.cuda_context == nullptr) {
        printf("Failed to initialize context: %d\n", err);
        return -1;
    }

    err = cuCtxSetCurrent(state.cuda_context);
    if (err) {
        printf("Failed to set context: %d\n", err);
        return -1;
    }

    err = cuStreamCreate(&state.cuda_stream, 1 /* non-blocking */);
    if (state.cuda_stream == nullptr) {
        printf("Failed to initialize stream: %d\n", err);
        return -1;
    }

    // Allocate some GPU memory on this context
    const int width = 32, height = 1024;

    void *ptr = nullptr;
    err = cuMemAlloc(&ptr, width * height * sizeof(float));

    if (ptr == nullptr) {
        printf("cuMemAlloc failed: %d\n", err);
        return -1;
    }

    // Point a buffer to it
    Buffer<float> in(width, height);
    in.fill(4.0f);
    auto device_interface = get_device_interface_for_device_api(DeviceAPI::CUDA);
    in.device_wrap_native(device_interface,
                          (uintptr_t)ptr, &state);
    in.set_host_dirty(true);
    in.set_device_dirty(false);
    in.copy_to_device(device_interface, &state);
    in.device_sync(&state);

    // Run a kernel on multiple threads that copies slices of it into
    // a Halide-allocated temporary buffer.  This would likely crash
    // if we don't allocate the outputs on the right context.
    Func f, g;
    Var x, xi, y;
    f(x, y) = sqrt(in(x, y));
    g(x, y) = f(x, y);
    f.gpu_tile(x, x, xi, 32).compute_at(g, y);
    g.parallel(y);

    for (int i = 0; i < 10; i++) {
        Buffer<float> out = g.realize(&state, {width, height});
        out.device_sync(&state);
        out.copy_to_host(&state);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float correct = 2.0f;
                if (out(x, y) != 2.0f) {
                    printf("out(%d, %d) = %f instead of %f\n", x, y, out(x, y), correct);
                    return -1;
                }
            }
        }
    }

    // Clean up
    in.device_detach_native(&state);
    cuMemFree(ptr);
    cuCtxDestroy(state.cuda_stream);

    if (state.acquires.load() != state.releases.load() ||
        state.acquires.load() < height) {
        printf("Context acquires: %d releases: %d\n", state.acquires.load(), state.releases.load());
        printf("Expected these to match and be at least %d (the number of parallel tasks)\n", height);
        return -1;
    }

    printf("Success!\n");
    return 0;
}

#endif
