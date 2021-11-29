#include "Halide.h"

using namespace Halide;

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
        handlers.custom_cuda_acquire_context = my_cuda_acquire_context;
        handlers.custom_cuda_release_context = my_cuda_release_context;
        handlers.custom_cuda_get_stream = my_cuda_get_stream;
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] CUDA not enabled.\n");
        return 0;
    }

    if (target.get_cuda_capability_lower_bound() < 61) {
        printf("[SKIP] Not running test on buildbot with very old GPU, as it fails for"
               " unknown reasons that we will probably never diagnose.\n");
        return 0;
    }

    {
        // Do some nonsense to get symbols out of libcuda without
        // needing the CUDA sdk. This would not be a concern in a real
        // cuda-using application but is helpful for our
        // build-and-test infrastructure.

        // We'll find cuda module in the Halide runtime so
        // that we can use it resolve symbols into libcuda in a
        // portable way.

        // Force-initialize the cuda runtime module by running something trivial.
        evaluate_may_gpu<float>(Expr(0.f));

        // Go get it, and dig out the method used to resolve symbols in libcuda.
        auto runtime_modules = Internal::JITSharedRuntime::get(nullptr, target, false);
        void *(*halide_cuda_get_symbol)(void *, const char *) = nullptr;
        for (Internal::JITModule &m : runtime_modules) {
            // Just rifle through all the runtime modules for this
            // target until we find the method we want.
            auto sym = m.find_symbol_by_name("halide_cuda_get_symbol");
            if (sym.address != nullptr) {
                halide_cuda_get_symbol = (decltype(halide_cuda_get_symbol))sym.address;
                break;
            }
        }

        if (halide_cuda_get_symbol == nullptr) {
            printf("Failed to extract halide_cuda_get_symbol from Halide cuda runtime\n");
            return -1;
        }

        // Go get the CUDA API functions we actually intend to use.
        cuStreamCreate = (decltype(cuStreamCreate))halide_cuda_get_symbol(nullptr, "cuStreamCreate");
        cuCtxCreate = (decltype(cuCtxCreate))halide_cuda_get_symbol(nullptr, "cuCtxCreate_v2");
        cuCtxDestroy = (decltype(cuCtxDestroy))halide_cuda_get_symbol(nullptr, "cuCtxDestroy_v2");
        cuCtxSetCurrent = (decltype(cuCtxSetCurrent))halide_cuda_get_symbol(nullptr, "cuCtxSetCurrent");
        cuMemAlloc = (decltype(cuMemAlloc))halide_cuda_get_symbol(nullptr, "cuMemAlloc_v2");
        cuMemFree = (decltype(cuMemFree))halide_cuda_get_symbol(nullptr, "cuMemFree_v2");

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

    // Make a cuda context and stream.
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

    // Wrap a Halide buffer around it, with some host memory too.
    Buffer<float> in(width, height);
    in.fill(4.0f);
    auto device_interface = get_device_interface_for_device_api(DeviceAPI::CUDA);
    in.device_wrap_native(device_interface,
                          (uintptr_t)ptr, &state);
    in.copy_to_device(device_interface, &state);

    // Run a kernel on multiple threads that copies slices of it into
    // a Halide-allocated temporary buffer.  This would likely crash
    // if we don't allocate the outputs on the right context. If the
    // copies don't happen on the same stream as the compute, we'll
    // get incorrect outputs due to race conditions.
    Func f, g;
    Var x, xi, y;
    f(x, y) = sqrt(in(x, y));
    g(x, y) = f(x, y);
    f.gpu_tile(x, x, xi, 32).compute_at(g, y);
    g.parallel(y);

    for (int i = 0; i < 10; i++) {
        Buffer<float> out = g.realize(&state, {width, height});
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
