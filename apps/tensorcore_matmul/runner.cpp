#include "Halide.h"
#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cstdio>

#include "matmul_cudaonly.h"
#include "matmul_tensorcore.h"

using Halide::float16_t;
using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

namespace {

constexpr int M = MATMUL_M, N = MATMUL_N, K = MATMUL_K;

bool check(const Buffer<const float16_t, 2> &A,
           const Buffer<const float16_t, 2> &B,
           const Buffer<const float, 2> &C,
           const char *name) {
    for (int y = 0; y < M; y += 97) {
        for (int x = 0; x < N; x += 89) {
            float ref = 0.f;
            for (int k = 0; k < K; k++) {
                ref += (float)A(k, y) * (float)B(x, k);
            }
            if (std::abs(C(x, y) - ref) > 1e-2f * std::max(1.f, std::abs(ref))) {
                printf("%s: bad result at %d %d: %f != %f\n", name, x, y, C(x, y), ref);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    const auto *interface = halide_cuda_device_interface();
    int major, minor;
    if (interface->compute_capability(nullptr, &major, &minor) != 0 ||
        major * 10 + minor < 70) {
        printf("[SKIP] Tensor cores require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    Buffer<float16_t, 2> A(K, M), B(N, K);
    A.fill([]() { return float16_t(((float)rand() / RAND_MAX) - 0.5f); });
    B.fill([]() { return float16_t(((float)rand() / RAND_MAX) - 0.5f); });

    Buffer<float, 2> C_cuda(N, M), C_tensorcore(N, M);

    matmul_cudaonly(A, B, C_cuda);
    C_cuda.copy_to_host();
    if (!check(A, B, C_cuda, "cudaonly")) {
        return 1;
    }

    matmul_tensorcore(A, B, C_tensorcore);
    C_tensorcore.copy_to_host();
    if (!check(A, B, C_tensorcore, "tensorcore")) {
        return 1;
    }

    // Two flops (a multiply and an add) per element of the reduction.
    const double flops = 2.0 * M * N * K;

    double t_cuda = benchmark([&]() {
        matmul_cudaonly(A, B, C_cuda);
        C_cuda.device_sync();
    });
    double t_tensorcore = benchmark([&]() {
        matmul_tensorcore(A, B, C_tensorcore);
        C_tensorcore.device_sync();
    });

    printf("cuda only:   %8.3f ms  %8.1f GFlop/s\n", t_cuda * 1e3, flops / t_cuda * 1e-9);
    printf("tensor core: %8.3f ms  %8.1f GFlop/s\n", t_tensorcore * 1e3, flops / t_tensorcore * 1e-9);
    printf("speed-up:    %8.2fx\n", t_cuda / t_tensorcore);

    printf("Success!\n");
    return 0;
}
