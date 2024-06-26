#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_thread_pool.h"

using namespace Halide;

int fib(int N, int a, int b) {
    while (N > 2) {
        a += b;
        std::swap(a, b);
        N--;
    }
    return b;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }
    if (target.has_feature(Target::D3D12Compute)) {
        // https://github.com/halide/Halide/issues/5000
        printf("[SKIP] Allocation cache not yet implemented for D3D12Compute.\n");
        return 0;
    }
    if (target.has_feature(Target::Vulkan) && ((target.os == Target::IOS) || target.os == Target::OSX)) {
        printf("[SKIP] Skipping test for Vulkan on iOS/OSX (MoltenVK only allows 30 buffers to be allocated)!\n");
        return 0;
    }
    if (target.has_feature(Target::WebGPU)) {
        printf("[SKIP] Allocation cache not yet implemented for WebGPU.\n");
        return 0;
    }
    const int N = 30;
    Var x, y, xi, yi;

    // Fixed size, overlapping lifetimes, looped 300 times. Should have 3 allocations live and OOM if there's a leak.
    Func f1[N];
    f1[0](x, y) = 1.0f;
    f1[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f1[1](x, y) = 2.0f;
    f1[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f1[i](x, y) = f1[i - 1](x, y) + f1[i - 2](x, y);
        f1[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    // Decreasing size, overlapping lifetimes, looped 300 times. Should OOM on leak.
    Func f2[N];
    f2[0](x, y) = 3.0f;
    f2[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f2[1](x, y) = 4.0f;
    f2[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f2[i](x, y) = f2[i - 1](x + 1, y) + f2[i - 2](x, y);
        f2[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    Func f3[N];
    f3[0](x, y) = 5.0f;
    f3[0].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    f3[1](x, y) = 6.0f;
    f3[1].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    for (int i = 2; i < N; i++) {
        f3[i](x, y) = f3[i - 1](x, clamp(y, 0, i)) + f3[i - 2](x, clamp(y, 0, i));
        f3[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    }

    float correct1 = fib(N, 1, 2), correct2 = fib(N, 3, 4), correct3 = fib(N, 5, 6);

    auto test1 = [&](bool use_cache, bool validate = true) {
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(use_cache);

        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f1[N - 1].realize({128, 128});
            if (validate) {
                result.copy_to_host();
                result.for_each_value([=](float f) {
                    if (f != correct1) {
                        printf("result is %f instead of %f\n", f, correct1);
                        abort();
                    }
                });
            } else {
                result.device_sync();
            }
        }
        // We don't want the cache to persist across these tests
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    auto test2 = [&](bool use_cache, bool validate = true) {
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(use_cache);

        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f2[N - 1].realize({128, 128});
            if (validate) {
                result.copy_to_host();
                result.for_each_value([=](float f) {
                    if (f != correct2) {
                        printf("result is %f instead of %f\n", f, correct2);
                        abort();
                    }
                });
            } else {
                result.device_sync();
            }
        }
        // We don't want the cache to persist across these tests
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    auto test3 = [&](bool use_cache, bool validate = true) {
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(use_cache);
        // Increasing size, overlapping lifetimes, looped 300 times. Should OOM on leak.
        for (int i = 0; i < 300; i++) {
            Buffer<float> result = f3[N - 1].realize({128, 128});
            if (validate) {
                result.copy_to_host();
                result.for_each_value([=](float f) {
                    if (f != correct3) {
                        printf("result is %f instead of %f\n", f, correct3);
                        abort();
                    }
                });
            } else {
                result.device_sync();
            }
        }
        // We don't want the cache to persist across these tests
        Halide::Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    // First run them serially (compilation of a Func isn't thread-safe).
    // test1(true);
    // test2(true);
    // test3(true);
    // return 0;

    // Now run all at the same time to check for concurrency issues.

    Halide::Tools::ThreadPool<void> pool(1);
    std::vector<std::future<void>> futures;
    futures.emplace_back(pool.async(test1, true));
    futures.emplace_back(pool.async(test1, true));
    futures.emplace_back(pool.async(test2, true));
    futures.emplace_back(pool.async(test2, true));
    futures.emplace_back(pool.async(test3, true));
    futures.emplace_back(pool.async(test3, true));
    for (auto &f : futures) {
        f.get();
    }

    // Vulkan will OOM unless allocation cache is used ... skip this since we just ran the same tests above concurrently
    if (!target.has_feature(Target::Vulkan)) {

        // Now benchmark with and without, (just informational, as this isn't a performance test)
        double t1 = Tools::benchmark([&]() {
            test1(true, false);
            test2(true, false);
            test3(true, false);
        });

        double t2 = Tools::benchmark([&]() {
            test1(false, false);
            test2(false, false);
            test3(false, false);
        });

        printf("Runtime with cache: %f\n"
               "Without cache: %f\n",
               t1, t2);
    }

    printf("Success!\n");
    return 0;
}
