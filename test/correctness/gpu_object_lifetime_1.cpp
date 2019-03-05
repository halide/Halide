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

    for (int i = 0; i < 2; i++) {
        Func f;
        f(x) = x;

        if (target.has_gpu_feature()) {
            f.gpu_tile(x, xi, 32);
        } else if (target.features_any_of({Target::HVX_64, Target::HVX})) {
            f.hexagon();
        }
        f.set_custom_print(halide_print);

        Buffer<int32_t> result = f.realize(256, target);
        for (int i = 0; i < 256; i++) {
            if (result(i) != i) {
                std::cout << "Error! " << result(i) << " != " << i << std::endl;
                return -1;
            }
        }
    }

    Halide::Internal::JITSharedRuntime::release_all();

    int ret = tracker.validate_gpu_object_lifetime(true /* allow_globals */, true /* allow_none */, 1 /* max_globals */);
    if (ret != 0) {
        return ret;
    }

    std::cout << "Success!" << std::endl;
    return 0;
}
