// TODO: this links libHalide purely to get Halide::float16_t, the same way
// apps/cuda_mat_mul does, and for the same reason: Float16.h is not
// distributed and its bodies live in libHalide.
#include "Halide.h"

#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cmath>
#include <cstdio>
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <vector>

#include "attention.h"

using Halide::float16_t;
using Halide::Runtime::Buffer;

namespace {

// The shape the filters were built for. The schedule is built around it, so it
// is a compile time constant on both sides, and the build passes the same
// numbers to the generator and to here.
#ifndef QUERIES
#define QUERIES 16384
#endif
#ifndef KEYS
#define KEYS 64
#endif
#ifndef DEPTH
#define DEPTH 64
#endif
#ifndef OUT_DEPTH
#define OUT_DEPTH 64
#endif
constexpr int queries = QUERIES, keys = KEYS, depth = DEPTH, out_depth = OUT_DEPTH;

// Both multiplies, which is what the fused filter does. The softmax is a few
// operations per score on top, and is not counted.
double gflops(double seconds) {
    double flops = 2.0 * queries * keys * depth + 2.0 * queries * out_depth * keys;
    return flops / seconds * 1e-9;
}

// Time one call. Both sides queue work asynchronously, so batch the launches
// and sync once rather than measuring a synchronize per launch. `sync` has to
// match the launcher: Halide runs on its own CUDA context.
template<typename F, typename S>
double bench(F &&launch, S &&sync) {
    const int batch = 5;
    return Halide::Tools::benchmark(5, 1,
                                    [&]() {
                                        for (int i = 0; i < batch; i++) {
                                            launch();
                                        }
                                        sync();
                                    }) /
           batch;
}

void fill(Buffer<float16_t, 2> &b, int modulus) {
    b.for_each_value([&](float16_t &v) { v = float16_t((float)(rand() % modulus)); });
    // for_each_value writes through the host pointer without saying so.
    b.set_host_dirty();
}

// Attention on the host, for a sample of rows. The tolerance is relative
// because the scores go through a half precision operand on the way into the
// second multiply, which is what the filter does too.
bool check(Buffer<float16_t, 2> &Q, Buffer<float16_t, 2> &K,
           Buffer<float16_t, 2> &V, Buffer<float, 2> &O, const char *name) {
    std::vector<float> score(keys);
    // A stride coprime with the rows per block, so the samples land at varying
    // offsets within a block.
    for (int y = 0; y < queries; y += 397) {
        float row_max = -1e30f;
        for (int j = 0; j < keys; j++) {
            score[j] = 0;
            for (int i = 0; i < depth; i++) {
                score[j] += (float)Q(i, y) * (float)K(i, j);
            }
            row_max = std::max(row_max, score[j]);
        }
        float total = 0;
        for (int j = 0; j < keys; j++) {
            score[j] = std::exp(score[j] - row_max);
            total += score[j];
        }
        for (int x = 0; x < out_depth; x++) {
            float correct = 0;
            for (int j = 0; j < keys; j++) {
                correct += (float)float16_t(score[j]) * (float)V(x, j);
            }
            correct /= total;
            if (std::abs(O(x, y) - correct) > 2e-3f * std::abs(correct) + 1e-5f) {
                printf("%s: bad result at %d %d: %f != %f\n", name, x, y,
                       (double)O(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

cublasHandle_t handle;

}  // namespace

int main(int argc, char **argv) {
    const auto *interface = halide_cuda_device_interface();
    assert(interface->compute_capability != nullptr);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    assert(err == 0);
    if (major * 10 + minor < 70) {
        printf("[SKIP] Tensor cores require compute capability 7.0 or above; "
               "this system has %d.%d.\n",
               major, minor);
        return 0;
    }

    Buffer<float16_t, 2> Q(depth, queries), K(depth, keys), V(out_depth, keys);
    Buffer<float, 2> O(out_depth, queries);
    // Small integers, so that the scores are exact and the only rounding is
    // the one the filter itself does going into the second multiply.
    fill(Q, 3);
    fill(K, 3);
    fill(V, 4);

    int failures = 0;
    if (attention(Q.raw_buffer(), K.raw_buffer(), V.raw_buffer(), O.raw_buffer()) != 0) {
        printf("filter returned an error\n");
        failures++;
    } else {
        O.copy_to_host();
        if (check(Q, K, V, O, "attention")) {
            double t = bench(
                [&]() {
                    attention(Q.raw_buffer(), K.raw_buffer(), V.raw_buffer(),
                              O.raw_buffer());
                },
                [&]() { O.device_sync(); });
            printf("  Halide fused attention        %9.0f GFlop/s\n", gflops(t));
        } else {
            failures++;
        }
    }

    // The two multiplies on their own, through a scores matrix in global
    // memory, with no softmax between them. It does less arithmetic than the
    // filters above but pays for the scores twice, so it is a throughput
    // reference rather than a baseline: the traffic it spends is the traffic
    // keeping the scores in registers exists to avoid.
    cublasCreate(&handle);
    float *scores = nullptr;
    cudaMalloc(&scores, (size_t)queries * keys * sizeof(float));
    void *Qd = (void *)halide_cuda_get_device_ptr(nullptr, Q.raw_buffer());
    void *Kd = (void *)halide_cuda_get_device_ptr(nullptr, K.raw_buffer());
    void *Vd = (void *)halide_cuda_get_device_ptr(nullptr, V.raw_buffer());
    void *Od = (void *)halide_cuda_get_device_ptr(nullptr, O.raw_buffer());
    static float alpha = 1.0f, beta = 0.0f;
    auto two_gemms = [&]() {
        // scores = K' Q, in the column major cublas sees Halide's buffers as.
        cublasGemmEx(handle, CUBLAS_OP_T, CUBLAS_OP_N, keys, queries, depth,
                     &alpha, Kd, CUDA_R_16F, depth, Qd, CUDA_R_16F, depth,
                     &beta, scores, CUDA_R_32F, keys, CUBLAS_COMPUTE_32F,
                     CUBLAS_GEMM_DEFAULT);
        // out = V scores.
        cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, out_depth, queries, keys,
                     &alpha, Vd, CUDA_R_16F, out_depth, scores, CUDA_R_32F, keys,
                     &beta, Od, CUDA_R_32F, out_depth, CUBLAS_COMPUTE_32F,
                     CUBLAS_GEMM_DEFAULT);
    };
    two_gemms();
    if (cudaDeviceSynchronize() != cudaSuccess) {
        printf("  (cublas reference did not run)\n");
    } else {
        double t = bench(two_gemms, []() { cudaDeviceSynchronize(); });
        printf("  cublas two gemms, no softmax  %9.0f GFlop/s\n", gflops(t));
    }
    cudaFree(scores);
    cublasDestroy(handle);

    if (failures) {
        printf("%d configuration(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
