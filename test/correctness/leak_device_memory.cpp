#include "Halide.h"
#include <stdio.h>

#include "test/common/gpu_object_lifetime_tracker.h"

using namespace Halide;

Internal::GpuObjectLifetimeTracker tracker;

void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    tracker.record_gpu_debug(str);
}

int main(int argc, char **argv) {

    Internal::JITHandlers handlers;
    handlers.custom_print = halide_print;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Target target = get_jit_target_from_environment();

    // We need debug output to record object creation.
    target.set_feature(Target::Debug);

    {
        Halide::Buffer<float> buf(100, 100);

        {
            // Make a shallow copy of the original buf, and trigger a gpu copy of it.
            Halide::Buffer<float> copy = buf;
            Func f;
            Var x, y;
            f(x, y) = copy(x, y);

            if (target.has_gpu_feature()) {
                Var xi, yi;
                f.gpu_tile(x, y, xi, yi, 8, 8);
            } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
                f.hexagon();
            }

            f.set_custom_print(halide_print);
            f.realize(50, 50, target);

            // The copy now has a non-zero dev field, but the original
            // buf is unaware of that fact. It should get cleaned up
            // here.
        }

        Halide::Internal::JITSharedRuntime::release_all();

        // At this point, the device allocation should have been cleaned up, even though the original buffer still lives.
        if (tracker.validate_gpu_object_lifetime(true /* allow_globals */,
                                                 true /* allow_none */,
                                                 1 /* max_globals */)) {
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
