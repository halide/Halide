#include "Halide.h"
#include <iostream>

#include "test/common/gpu_object_lifetime_tracker.h"

using namespace Halide;

Internal::GpuObjectLifetimeTracker tracker;

void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    tracker.record_gpu_debug(str);
}

int main(int argc, char *argv[]) {
    Var x, xi;

    Internal::JITHandlers handlers;
    handlers.custom_print = halide_print;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Target target = get_jit_target_from_environment();

    // We need debug output to record object creation.
    target.set_feature(Target::Debug);

    {
        // Verify that internal buffers in a sequence of device stages are
        // released. This should generate some early frees of buffers with
        // device allocations.
        const int stage_count = 10;
        Func f[stage_count];
        f[0](x) = x;
        for (int i = 1; i < stage_count; i++) {
            f[i](x) = f[i - 1](x);
        }

        for (int i = 0; i < stage_count; i++) {
            f[i].compute_root();

            if (i % 3 != 0) {
                if (target.has_gpu_feature()) {
                    f[i].gpu_tile(x, xi, 32);
                } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
                    f[i].hexagon();
                }
            }
        }

        Func output = f[stage_count - 1];

        output.set_custom_print(halide_print);

        output.realize(256, target);
    }

    Internal::JITSharedRuntime::release_all();

    int ret = tracker.validate_gpu_object_lifetime(true /* allow_globals */, true /* allow_none */, 1 /* max_globals */);
    if (ret != 0) {
        return ret;
    }

    std::cout << "Success!" << std::endl;
    return 0;
}
