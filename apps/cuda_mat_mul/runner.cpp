#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cmath>
#include <cstdio>
#include <cublas_v2.h>
#include <cuda_runtime.h>

#include "mat_mul.h"
#include "mat_mul_f16.h"

using Halide::Runtime::Buffer;

namespace {

// The same matrix multiply is compiled twice from one generator: once with
// float operands, which get a schedule that accumulates in ordinary registers,
// and once with half operands, which get the tensor cores. Both accumulate in
// and return single precision, so the two are directly comparable.

template<typename T>
bool check(const Buffer<T, 2> &A, const Buffer<T, 2> &B,
           const Buffer<float, 2> &C, int size, const char *name) {
    // Spot check on strides that are coprime with the tile sizes, so the
    // samples land at varying offsets within a tile.
    for (int y = 0; y < size; y += 97) {
        for (int x = 0; x < size; x += 89) {
            float correct = 0.f;
            for (int k = 0; k < size; k++) {
                correct += (float)A(x, k) * (float)B(k, y);
            }
            // The operands are small integers, which are exact in both float
            // and half, and the accumulator is single precision either way, so
            // the answer should be exact.
            if (C(x, y) != correct) {
                printf("%s: bad result at %d %d: %f != %f\n",
                       name, x, y, C(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

double gflops(int size, double seconds) {
    return 2.0 * size * size * size / seconds * 1e-9;
}

// Time a batch of launches with a single synchronization at the end, rather
// than synchronizing after each one. Both implementations queue work
// asynchronously, and cublas in particular does a heuristic lookup on the host
// for every call, so synchronizing per launch measures that host work instead
// of letting it overlap with the GPU.
// `sync` has to match the launcher: Halide runs on its own CUDA context, so
// cudaDeviceSynchronize does not wait for it.
template<typename F, typename S>
double bench_batched(F &&launch, S &&sync) {
    const int samples = 5, iterations = 5;
    for (int i = 0; i < iterations; i++) {
        launch();
    }
    sync();
    double best = 0;
    for (int s = 0; s < samples; s++) {
        auto t0 = Halide::Tools::benchmark_now();
        for (int i = 0; i < iterations; i++) {
            launch();
        }
        sync();
        auto t1 = Halide::Tools::benchmark_now();
        double t = Halide::Tools::benchmark_duration_seconds(t0, t1) / iterations;
        if (s == 0 || t < best) {
            best = t;
        }
    }
    return best;
}

}  // namespace

int main(int argc, char **argv) {
    const auto *interface = halide_cuda_device_interface();
    assert(interface->compute_capability != nullptr);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    assert(err == 0);
    int ver = major * 10 + minor;
    if (ver < 50) {
        printf("[SKIP] This system supports only Cuda compute capability %d.%d, but compute capability 5.0+ is required.\n", major, minor);
        return 0;
    }

    int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    {
        Buffer<float, 2> A(size, size), B(size, size), C(size, size);
        A.for_each_value([](float &v) { v = (float)((rand() & 3) - 1); });
        B.for_each_value([](float &v) { v = (float)((rand() & 3) - 1); });
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul(A, B, C);
        C.copy_to_host();
        if (!check(A, B, C, size, "float")) {
            return 1;
        }

        double t = bench_batched([&]() { mat_mul(A, B, C); },
                                 [&]() { C.device_sync(); });
        printf("Halide float: %f s (%.1f GFlop/s)\n", t, gflops(size, t));
    }

    // The half variant is scheduled onto the tensor cores.
    if (ver < 70) {
        printf("[SKIP] Tensor cores require compute capability 7.0 or above; "
               "this system has %d.%d.\n",
               major, minor);
    } else {
        // _Float16 rather than Halide::float16_t, so that this stays a
        // runtime-only program that doesn't link the compiler.
        Buffer<_Float16, 2> A(size, size), B(size, size);
        Buffer<float, 2> C(size, size);
        A.for_each_value([](_Float16 &v) { v = (_Float16)((rand() & 3) - 1); });
        B.for_each_value([](_Float16 &v) { v = (_Float16)((rand() & 3) - 1); });
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul_f16(A, B, C);
        C.copy_to_host();
        if (!check(A, B, C, size, "half")) {
            return 1;
        }

        double t = bench_batched([&]() { mat_mul_f16(A, B, C); },
                                 [&]() { C.device_sync(); });
        printf("Halide half (tensor cores): %f s (%.1f GFlop/s)\n",
               t, gflops(size, t));
    }

    // Benchmark cublas for reference, at both precisions. The half precision
    // one accumulates in single precision, matching what the Halide pipeline
    // does, so the two are comparable.
#ifdef _MSC_VER
    // https://github.com/halide/Halide/issues/5053
    printf("Skipping cublas on Windows; see https://github.com/halide/Halide/issues/5053\n");
#else
    {
        void *A, *B, *C;
        cudaMalloc(&A, (size_t)size * size * 4);
        cudaMalloc(&B, (size_t)size * size * 4);
        cudaMalloc(&C, (size_t)size * size * 4);
        // Touch the memory before timing anything, so that no part of the
        // benchmark pays for faulting it in, and so that the operands are
        // definite values rather than whatever was there. This byte pattern is
        // a normal number read either as float or as half, which matters
        // because denormals can be slow.
        cudaMemset(A, 0x3c, (size_t)size * size * 4);
        cudaMemset(B, 0x3c, (size_t)size * size * 4);
        cudaMemset(C, 0, (size_t)size * size * 4);
        cublasHandle_t handle;
        cublasCreate(&handle);
        float alpha = 1.0f, beta = 1.0f;

        double t = bench_batched([&]() { cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                                     size, size, size, &alpha, (const float *)A, size,
                                                     (const float *)B, size, &beta, (float *)C, size); },
                                 []() { cudaDeviceSynchronize(); });
        printf("cublas float: %f s (%.1f GFlop/s)\n", t, gflops(size, t));

        if (ver >= 70) {
            // Half precision operands into a single precision accumulator,
            // which is what the tensor cores do natively.
            t = bench_batched([&]() { cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                                   size, size, size, &alpha,
                                                   A, CUDA_R_16F, size,
                                                   B, CUDA_R_16F, size, &beta,
                                                   C, CUDA_R_32F, size,
                                                   CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT); },
                              []() { cudaDeviceSynchronize(); });
            printf("cublas half: %f s (%.1f GFlop/s)\n", t, gflops(size, t));
        }

        cudaFree(A);
        cudaFree(B);
        cudaFree(C);
        cublasDestroy(handle);
    }
#endif

    printf("Success!\n");
    return 0;
}
