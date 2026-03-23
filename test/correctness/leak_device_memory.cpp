#include "Halide.h"
#include "gpu_object_lifetime_tracker.h"

#include <cstdio>

using namespace Halide;

Internal::GpuObjectLifetimeTracker tracker;

void halide_print(JITUserContext *user_context, const char *str) {
    printf("%s", str);

    tracker.record_gpu_debug(str);
}

int main(int argc, char **argv) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    printf("[SKIP] Serialization won't preserve GPU buffers, skipping.\n");
    return 0;
#endif

    Target target = get_jit_target_from_environment();

    // We need debug output to record object creation.
    target.set_feature(Target::Debug);

    // We need to hook the default handler too, to catch the frees done by release_all
    JITHandlers handlers;
    handlers.custom_print = halide_print;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    // This tests what happens when you make a shallow copy of a
    // Runtime::Buffer and then give the copy a device
    // allocation. This is a silly thing to do, but we should at least
    // not leak device memory.
    {
        Halide::Runtime::Buffer<float> buf(100, 100);

        {
            // Make a shallow copy of the original buf, put it in a
            // Halide::Buffer, and then run a Pipeline that triggers a
            // gpu copy of it.
            Halide::Runtime::Buffer<float> shallow_copy = buf;
            Halide::Buffer<float> copy(std::move(shallow_copy));
            Func f;
            Var x, y;
            f(x, y) = copy(x, y);

            if (target.has_gpu_feature()) {
                Var xi, yi;
                f.gpu_tile(x, y, xi, yi, 8, 8);
            } else if (target.has_feature(Target::HVX)) {
                f.hexagon();
            }

            f.realize({50, 50}, target);

            // The copy now has a non-zero dev field, but the original
            // buf is unaware of that fact. It should get cleaned up
            // here.
            if (target.has_gpu_feature()) {
                assert(copy.has_device_allocation());
            }
        }

        Halide::Internal::JITSharedRuntime::release_all();

        assert(!buf.has_device_allocation());

        // At this point, the device allocation should have been cleaned up, even though the original buffer still lives.
        if (tracker.validate_gpu_object_lifetime(true /* allow_globals */,
                                                 true /* allow_none */,
                                                 1 /* max_globals */)) {
            fprintf(stderr, "validate_gpu_object_lifetime() failed\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
