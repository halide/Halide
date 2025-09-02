#include "Halide.h"
#include "halide_thread_pool.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int fib(int N, int a, int b) {
    while (N > 2) {
        a += b;
        std::swap(a, b);
        N--;
    }
    return b;
}

class GPUAllocationCacheTest : public testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    static constexpr int N = 30;
    static constexpr int kIters = 300;
    Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};

    void SetUp() override {
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
        if (target.has_feature(Target::D3D12Compute)) {
            // TODO: https://github.com/halide/Halide/issues/5000
            GTEST_SKIP() << "Allocation cache not yet implemented for D3D12Compute.";
        }
        if (target.has_feature(Target::Vulkan) && (target.os == Target::IOS || target.os == Target::OSX)) {
            // TODO: open an issue to track if this restriction is ever lifted
            GTEST_SKIP() << "Skipping test for Vulkan on iOS/OSX (MoltenVK only allows 30 buffers to be allocated)!";
        }
        if (target.has_feature(Target::Vulkan) && target.os == Target::Windows) {
            GTEST_SKIP() << "Skipping test for Vulkan on Windows ... fails unless run on its own!";
        }
        if (target.has_feature(Target::WebGPU)) {
            GTEST_SKIP() << "Allocation cache not yet implemented for WebGPU.";
        }
    }
};
}  // namespace

TEST_F(GPUAllocationCacheTest, Basic) {
    // Fixed size, overlapping lifetimes, looped kIters times. Should have 3 allocations live and OOM if there's a leak.
    Func f1[N];
    f1[0](x, y) = 1.0f;
    f1[1](x, y) = 2.0f;
    for (int i = 2; i < N; i++) {
        f1[i](x, y) = f1[i - 1](x, y) + f1[i - 2](x, y);
    }

    // Decreasing size, overlapping lifetimes, looped kIters times. Should OOM on leak.
    Func f2[N];
    f2[0](x, y) = 3.0f;
    f2[1](x, y) = 4.0f;
    for (int i = 2; i < N; i++) {
        f2[i](x, y) = f2[i - 1](x + 1, y) + f2[i - 2](x, y);
    }

    // Increasing size, overlapping lifetimes, looped kIters times. Should OOM on leak.
    Func f3[N];
    f3[0](x, y) = 5.0f;
    f3[1](x, y) = 6.0f;
    for (int i = 2; i < N; i++) {
        f3[i](x, y) = f3[i - 1](x, clamp(y, 0, i)) + f3[i - 2](x, clamp(y, 0, i));
    }

    for (Func *fs : {f1, f2, f3}) {
        for (int i = 0; i < N; i++) {
            fs[i].compute_root().gpu_tile(x, y, xi, yi, 8, 8);
        }
    }

    float correct1 = fib(N, 1, 2);
    float correct2 = fib(N, 3, 4);
    float correct3 = fib(N, 5, 6);

    std::optional<float> test1_incorrect{};
    std::optional<float> test2_incorrect{};
    std::optional<float> test3_incorrect{};

    bool use_cache = true;

    auto test_func = [&](Func f, float correct, std::optional<float> &incorrect) {
        Internal::JITSharedRuntime::reuse_device_allocations(use_cache);

        for (int i = 0; i < kIters; i++) {
            Buffer<float> result = f.realize({128, 128});
            result.copy_to_host();
            result.for_each_value([&](float value) {
                // Can't run GTest macros on other threads.
                if (value != correct) {
                    incorrect = value;
                }
            });
        }
        // We don't want the cache to persist across these tests
        Internal::JITSharedRuntime::reuse_device_allocations(false);
    };

    Tools::ThreadPool<void> pool(1);
    std::vector<std::future<void>> futures;
    futures.emplace_back(pool.async(test_func, f1[N - 1], correct1, test1_incorrect));
    futures.emplace_back(pool.async(test_func, f1[N - 1], correct1, test1_incorrect));
    futures.emplace_back(pool.async(test_func, f2[N - 1], correct2, test2_incorrect));
    futures.emplace_back(pool.async(test_func, f2[N - 1], correct2, test2_incorrect));
    futures.emplace_back(pool.async(test_func, f3[N - 1], correct3, test3_incorrect));
    futures.emplace_back(pool.async(test_func, f3[N - 1], correct3, test3_incorrect));
    for (auto &f : futures) {
        f.get();
    }

    EXPECT_EQ(test1_incorrect, std::nullopt);
    EXPECT_EQ(test2_incorrect, std::nullopt);
    EXPECT_EQ(test3_incorrect, std::nullopt);
}
