#include "Halide.h"

#include <atomic>

using namespace Halide;

// The runtime's default context-management functions, looked up from the JIT'd
// WebGPU runtime module. Our custom handlers delegate to these so that we can
// verify the custom accessors are actually invoked without needing to link
// against the WebGPU SDK. All WebGPU handles are modelled as void * here to
// avoid a dependence on the WebGPU headers.
static int (*default_webgpu_acquire_context)(void *, void **, void **, void **, void **, bool) = nullptr;
static int (*default_webgpu_release_context)(void *) = nullptr;

struct WebGPUState : public Halide::JITUserContext {
    std::atomic<int> acquires = 0, releases = 0;

    static int my_acquire_context(JITUserContext *ctx, void **instance, void **adapter,
                                  void **device, void **staging_buffer, bool create) {
        WebGPUState *state = (WebGPUState *)ctx;
        state->acquires++;
        return default_webgpu_acquire_context(ctx, instance, adapter, device, staging_buffer, create);
    }

    static int my_release_context(JITUserContext *ctx) {
        WebGPUState *state = (WebGPUState *)ctx;
        state->releases++;
        return default_webgpu_release_context(ctx);
    }

    WebGPUState() {
        handlers.custom_webgpu_acquire_context = my_acquire_context;
        handlers.custom_webgpu_release_context = my_release_context;
    }
};

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::WebGPU)) {
        printf("[SKIP] WebGPU not enabled.\n");
        return 0;
    }

    // Force-initialize the WebGPU runtime module by running something trivial,
    // then extract the default context-management functions from it.
    evaluate_may_gpu<float>(Expr(0.f));

    default_webgpu_acquire_context = (int (*)(void *, void **, void **, void **, void **, bool))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_webgpu_acquire_context");
    default_webgpu_release_context = (int (*)(void *))
        Internal::JITSharedRuntime::find_symbol(target, "halide_default_webgpu_release_context");

    if (default_webgpu_acquire_context == nullptr || default_webgpu_release_context == nullptr) {
        printf("Failed to extract default WebGPU context functions from runtime\n");
        return 1;
    }

    // Run a kernel on multiple threads, using our custom context accessors on
    // every acquire/release. This would likely crash or produce incorrect
    // results if the accessors were not being invoked consistently.
    const int width = 32, height = 256;
    Buffer<float> in(width, height);
    in.fill(4.0f);

    WebGPUState state;

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
