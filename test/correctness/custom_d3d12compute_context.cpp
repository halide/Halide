#include "Halide.h"

#include <atomic>

using namespace Halide;

// The runtime's default context-management functions, looked up from the JIT'd
// Direct3D 12 Compute runtime module. Our custom handlers delegate to these so
// that we can verify the custom accessors are actually invoked without needing
// to link against the Direct3D SDK.
static int (*default_d3d12compute_acquire_context)(void *, void **, void **, bool) = nullptr;
static int (*default_d3d12compute_release_context)(void *) = nullptr;

struct D3D12ComputeState : public Halide::JITUserContext {
    std::atomic<int> acquires = 0, releases = 0;

    static int my_acquire_context(JITUserContext *ctx, void **device, void **queue, bool create) {
        D3D12ComputeState *state = (D3D12ComputeState *)ctx;
        state->acquires++;
        return default_d3d12compute_acquire_context(ctx, device, queue, create);
    }

    static int my_release_context(JITUserContext *ctx) {
        D3D12ComputeState *state = (D3D12ComputeState *)ctx;
        state->releases++;
        return default_d3d12compute_release_context(ctx);
    }

    D3D12ComputeState() {
        handlers.custom_d3d12compute_acquire_context = my_acquire_context;
        handlers.custom_d3d12compute_release_context = my_release_context;
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::D3D12Compute)) {
        printf("[SKIP] D3D12Compute not enabled.\n");
        return 0;
    }

    // Force-initialize the D3D12 runtime module by running something trivial,
    // then extract the default context-management functions from it.
    evaluate_may_gpu<float>(Expr(0.f));

    default_d3d12compute_acquire_context = (int (*)(void *, void **, void **, bool))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_d3d12compute_acquire_context");
    default_d3d12compute_release_context = (int (*)(void *))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_d3d12compute_release_context");

    if (default_d3d12compute_acquire_context == nullptr || default_d3d12compute_release_context == nullptr) {
        printf("Failed to extract default D3D12Compute context functions from runtime\n");
        return 1;
    }

    // Run a kernel on multiple threads, using our custom context accessors on
    // every acquire/release. This would likely crash or produce incorrect
    // results if the accessors were not being invoked consistently.
    const int width = 32, height = 256;
    Buffer<float> in(width, height);
    in.fill(4.0f);

    D3D12ComputeState state;

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
