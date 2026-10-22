#include "Halide.h"

#include <atomic>

using namespace Halide;

// The runtime's default context-management functions, looked up from the JIT'd
// OpenCL runtime module. Our custom handlers delegate to these so that we can
// verify the custom accessors are actually invoked without needing to link
// against the OpenCL SDK.
static int (*default_acquire_cl_context)(void *, void **, void **, bool) = nullptr;
static int (*default_release_cl_context)(void *) = nullptr;

struct OpenCLState : public Halide::JITUserContext {
    std::atomic<int> acquires = 0, releases = 0;

    static int my_acquire_context(JITUserContext *ctx, void **cl_ctx, void **cl_q, bool create) {
        OpenCLState *state = (OpenCLState *)ctx;
        state->acquires++;
        return default_acquire_cl_context(ctx, cl_ctx, cl_q, create);
    }

    static int my_release_context(JITUserContext *ctx) {
        OpenCLState *state = (OpenCLState *)ctx;
        state->releases++;
        return default_release_cl_context(ctx);
    }

    OpenCLState() {
        handlers.custom_opencl_acquire_context = my_acquire_context;
        handlers.custom_opencl_release_context = my_release_context;
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenCL)) {
        printf("[SKIP] OpenCL not enabled.\n");
        return 0;
    }

    // Force-initialize the OpenCL runtime module by running something trivial,
    // then extract the default context-management functions from it.
    evaluate_may_gpu<float>(Expr(0.f));

    default_acquire_cl_context = (int (*)(void *, void **, void **, bool))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_acquire_cl_context");
    default_release_cl_context = (int (*)(void *))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_release_cl_context");

    if (default_acquire_cl_context == nullptr || default_release_cl_context == nullptr) {
        printf("Failed to extract default OpenCL context functions from runtime\n");
        return 1;
    }

    // Run a kernel on multiple threads, using our custom context accessors on
    // every acquire/release. This would likely crash or produce incorrect
    // results if the accessors were not being invoked consistently.
    const int width = 32, height = 256;
    Buffer<float> in(width, height);
    in.fill(4.0f);

    OpenCLState state;

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
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %f instead of %f\n", x, y, out(x, y), correct);
                    return 1;
                }
            }
        }
    }

    if (state.acquires.load() != state.releases.load() || state.acquires.load() == 0) {
        printf("Context acquires: %d releases: %d\n", state.acquires.load(), state.releases.load());
        printf("Expected these to match and be nonzero\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
