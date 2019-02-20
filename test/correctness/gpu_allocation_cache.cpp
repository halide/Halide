#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    const int N = 10;
    Var x, y, xi, yi;

    // Fixed size, overlapping lifetimes, looped 300 times. Should have 3 allocations live and OOM if there's a leak.
    Func f1[N];
    f1[0](x, y) = 0.0f;
    f1[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f1[1](x, y) = 0.0f;
    f1[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f1[i](x, y) = f1[i-1](x, y) + f1[i-2](x, y);
        f1[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    // Decreasing size, overlapping lifetimes, looped 300 times. Should OOM on leak.
    Func f2[N];
    f2[0](x, y) = 0.0f;
    f2[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f2[1](x, y) = 0.0f;
    f2[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f2[i](x, y) = f2[i-1](x+1, y) + f2[i-2](x, y);
        f2[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    Func f3[N];
    f3[0](x, y) = 0.0f;
    f3[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f3[1](x, y) = 0.0f;
    f3[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f3[i](x, y) = f3[i-1](x, clamp(y, 0, i)) + f3[i-2](x, clamp(y, 0, i));
        f3[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    Halide::Internal::JITSharedRuntime::reuse_device_allocations(true);

    auto test1 = [&](){
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(true);

        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f1[N-1].realize(1024, 1024);
            result.device_sync();
        }
        // We don't want the cache to persist across these tests
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    auto test2 = [&](){
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(true);

        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f2[N-1].realize(1024, 1024);
            result.device_sync();
        }
        // We don't want the cache to persist across these tests
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    auto test3 = [&]() {
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(true);
        // Increasing size, overlapping lifetimes, looped 300 times. Should OOM on leak.
        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f3[N-1].realize(1024, 1024);
            result.device_sync();
        }
        // We don't want the cache to persist across these tests
        //Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    // First run them serially.
    test1();
    test2();
    test3();

    // Now run all at the same time to check for concurrency issues
    {
        Halide::Internal::ThreadPool<void> pool;
        std::vector<std::future<void>> futures;
        futures.emplace_back(pool.async(test1));
        futures.emplace_back(pool.async(test2));
        futures.emplace_back(pool.async(test3));
        futures.emplace_back(pool.async(test1));
        futures.emplace_back(pool.async(test2));
        futures.emplace_back(pool.async(test3));
        for (auto &f : futures) {
            f.get();
        }
    }

    printf("Success!\n");
    return 0;
}
