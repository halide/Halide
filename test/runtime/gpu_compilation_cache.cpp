#include "HalideRuntime.h"

#include "common.h"
#include "gpu_context_common.h"
#include "printer.h"

using namespace Halide::Runtime::Internal;

namespace {

struct FakeModule {
    int id;
    int free_count;
};

struct FreeTracker {
    void *user_context;
    int total_frees = 0;
    int null_frees = 0;

    void operator()(FakeModule *mod) {
        if (mod == nullptr) {
            null_frees++;
            return;
        }
        mod->free_count++;
        total_frees++;
    }
};

}  // namespace

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // Test 1: Tombstones from delete_context are skipped by subsequent
    // delete_context / release_all calls (Issue #9564).
    {
        Halide::Internal::GPUCompilationCache<void *, FakeModule *> cache;
        FreeTracker free_tracker{user_context};

        void *ctx1 = (void *)(uintptr_t)0x1000;
        void *ctx2 = (void *)(uintptr_t)0x2000;
        void *ctx3 = (void *)(uintptr_t)0x3000;

        FakeModule m1{1, 0};
        FakeModule m2{2, 0};
        FakeModule m3{3, 0};
        FakeModule m4{4, 0};

        void *state1 = nullptr;
        void *state2 = nullptr;
        void *state3 = nullptr;
        void *state4 = nullptr;

        FakeModule *out = nullptr;
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state1, ctx1, out, [&]() { return &m1; }));
        HALIDE_CHECK(user_context, out == &m1);
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state2, ctx1, out, [&]() { return &m2; }));
        HALIDE_CHECK(user_context, out == &m2);
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state3, ctx2, out, [&]() { return &m3; }));
        HALIDE_CHECK(user_context, out == &m3);
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state4, ctx3, out, [&]() { return &m4; }));
        HALIDE_CHECK(user_context, out == &m4);

        cache.release_hold(user_context, ctx1, state1);
        cache.release_hold(user_context, ctx1, state2);
        cache.release_hold(user_context, ctx2, state3);
        cache.release_hold(user_context, ctx3, state4);

        // Delete ctx1: frees m1 and m2, leaving two tombstones while ctx2 and ctx3 keep the table alive.
        cache.delete_context(user_context, ctx1, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 2);
        HALIDE_CHECK(user_context, m1.free_count == 1);
        HALIDE_CHECK(user_context, m2.free_count == 1);
        HALIDE_CHECK(user_context, !cache.lookup(ctx1, state1, out));
        HALIDE_CHECK(user_context, !cache.lookup(ctx1, state2, out));
        HALIDE_CHECK(user_context, cache.lookup(ctx2, state3, out) && out == &m3);
        HALIDE_CHECK(user_context, cache.lookup(ctx3, state4, out) && out == &m4);

        // Deleting ctx1 again must not revisit its tombstones or corrupt count.
        cache.delete_context(user_context, ctx1, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 2);

        // Delete ctx2: if count had been decremented twice for ctx1's tombstones,
        // count would now be 0 and release_all below would skip freeing m4!
        cache.delete_context(user_context, ctx2, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 3);
        HALIDE_CHECK(user_context, m3.free_count == 1);
        HALIDE_CHECK(user_context, !cache.lookup(ctx2, state3, out));
        HALIDE_CHECK(user_context, cache.lookup(ctx3, state4, out) && out == &m4);

        // Release all: must skip ctx1 and ctx2 tombstones and release ctx3's m4 exactly once.
        cache.release_all(user_context, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 4);
        HALIDE_CHECK(user_context, m1.free_count == 1);
        HALIDE_CHECK(user_context, m2.free_count == 1);
        HALIDE_CHECK(user_context, m3.free_count == 1);
        HALIDE_CHECK(user_context, m4.free_count == 1);
        HALIDE_CHECK(user_context, !cache.lookup(ctx3, state4, out));
    }

    // Test 2: Same context with one entry held (use_count > 0) and one released,
    // followed by re-insertion into tombstoned slot and release_all.
    {
        Halide::Internal::GPUCompilationCache<void *, FakeModule *> cache;
        FreeTracker free_tracker{user_context};

        void *ctx = (void *)(uintptr_t)0x1000;
        FakeModule m1{1, 0};
        FakeModule m2{2, 0};
        FakeModule m1_recompiled{3, 0};

        void *state1 = nullptr;
        void *state2 = nullptr;
        FakeModule *out = nullptr;

        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state1, ctx, out, [&]() { return &m1; }));
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state2, ctx, out, [&]() { return &m2; }));

        // Release hold only on state1; state2 remains in use (use_count == 1).
        cache.release_hold(user_context, ctx, state1);

        // delete_context frees m1 (tombstoned) while m2 stays cached.
        cache.delete_context(user_context, ctx, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 1);
        HALIDE_CHECK(user_context, m1.free_count == 1);
        HALIDE_CHECK(user_context, m2.free_count == 0);
        HALIDE_CHECK(user_context, !cache.lookup(ctx, state1, out));
        HALIDE_CHECK(user_context, cache.lookup(ctx, state2, out) && out == &m2);

        // Release hold on state2, then release_all: must free only m2 and skip m1's tombstone.
        cache.release_hold(user_context, ctx, state2);
        cache.release_all(user_context, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 2);
        HALIDE_CHECK(user_context, m1.free_count == 1);
        HALIDE_CHECK(user_context, m2.free_count == 1);

        // Re-setup state1 after release: should recompile and cache cleanly.
        HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &state1, ctx, out, [&]() { return &m1_recompiled; }));
        HALIDE_CHECK(user_context, out == &m1_recompiled);
        HALIDE_CHECK(user_context, cache.lookup(ctx, state1, out) && out == &m1_recompiled);
        cache.release_hold(user_context, ctx, state1);
        cache.release_all(user_context, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 3);
        HALIDE_CHECK(user_context, m1_recompiled.free_count == 1);
    }

    // Test 3: Table resizing past initial capacity with tombstones present.
    {
        Halide::Internal::GPUCompilationCache<void *, FakeModule *> cache;
        FreeTracker free_tracker{user_context};

        constexpr int kNumKernels = 200;
        void *ctx_evict = (void *)(uintptr_t)0xAAAA;
        void *ctx_keep = (void *)(uintptr_t)0xBBBB;

        FakeModule evict_modules[32];
        void *evict_states[32] = {};
        for (int i = 0; i < 32; i++) {
            evict_modules[i] = {i + 1, 0};
            FakeModule *out = nullptr;
            HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &evict_states[i], ctx_evict, out,
                                                                [&]() { return &evict_modules[i]; }));
            cache.release_hold(user_context, ctx_evict, evict_states[i]);
        }

        FakeModule keep_modules[kNumKernels];
        void *keep_states[kNumKernels] = {};
        // Insert first 16 keep modules, then delete ctx_evict to create 32 tombstones before resizing.
        for (int i = 0; i < 16; i++) {
            keep_modules[i] = {1000 + i, 0};
            FakeModule *out = nullptr;
            HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &keep_states[i], ctx_keep, out,
                                                                [&]() { return &keep_modules[i]; }));
            cache.release_hold(user_context, ctx_keep, keep_states[i]);
        }

        cache.delete_context(user_context, ctx_evict, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 32);

        // Insert remaining keep modules, forcing multiple table resizes.
        for (int i = 16; i < kNumKernels; i++) {
            keep_modules[i] = {1000 + i, 0};
            FakeModule *out = nullptr;
            HALIDE_CHECK(user_context, cache.kernel_state_setup(user_context, &keep_states[i], ctx_keep, out,
                                                                [&]() { return &keep_modules[i]; }));
            cache.release_hold(user_context, ctx_keep, keep_states[i]);
        }

        for (int i = 0; i < kNumKernels; i++) {
            FakeModule *out = nullptr;
            HALIDE_CHECK(user_context, cache.lookup(ctx_keep, keep_states[i], out));
            HALIDE_CHECK(user_context, out == &keep_modules[i]);
        }

        cache.release_all(user_context, free_tracker);
        HALIDE_CHECK(user_context, free_tracker.null_frees == 0);
        HALIDE_CHECK(user_context, free_tracker.total_frees == 32 + kNumKernels);
        for (int i = 0; i < kNumKernels; i++) {
            HALIDE_CHECK(user_context, keep_modules[i].free_count == 1);
        }
    }

    print(user_context) << "Success!\n";
    return 0;
}
