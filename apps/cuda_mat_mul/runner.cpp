#include "Halide.h"
#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include "mat_mul_50.h"
#include "mat_mul_70.h"
#include <cstdio>
#include <cublas_v2.h>
#include <cuda_runtime.h>

using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

int main(int argc, char **argv) {
    // Our Generator is compiled using cuda_capability_50; if the system running this
    // test doesn't have at least that, quietly skip the test.
    const auto *interface = halide_cuda_device_interface();
    assert(interface->compute_capability != nullptr);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    assert(err == 0);
    int cuda_version = major * 10 + minor;
    if (cuda_version < 50) {
        printf("[SKIP] This system supports only Cuda compute capability %d.%d, but compute capability 5.0+ is required.\n", major, minor);
        return 0;
    }

    int size = 2048;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    // Check correctness using small-integer matrices
    if (1) {
        Buffer<Halide::float16_t> A(size, size), B(size, size);
        Buffer<float> C(size, size);
        A.for_each_value([](Halide::float16_t &v) { v = static_cast<Halide::float16_t>((rand() & 3) - 1); });
        B.for_each_value([](Halide::float16_t &v) { v = static_cast<Halide::float16_t>((rand() & 3) - 1); });
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul_50(A, B, C);
        C.copy_to_host();
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float correct = 0.f;
                for (int k = 0; k < size; k++) {
                    correct += static_cast<float>(A(x, k)) * static_cast<float>(B(k, y));
                }
                float actual = C(x, y);
                if (correct != actual) {
                    printf("%d %d: %f vs %f\n", x, y, correct, actual);
                    return -1;
                }
            }
        }
    }

    const float gflops = 2.0f * size * size * size / 1e9f;

    // Benchmark it
    {
        Buffer<Halide::float16_t> A(size, size), B(size, size);
        Buffer<float> C(size, size);
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            mat_mul_50(A, B, C);
            C.device_sync();
        });
        printf("Halide time (CUDA 5.0): %f (ms) GFLOPS: %f \n", t * 1000.0, gflops / t);
    }

    if (cuda_version >= 70)
    {
        Buffer<Halide::float16_t> A(size, size), B(size, size);
        Buffer<float> C(size, size);
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            mat_mul_70(A, B, C);
            C.device_sync();
        });
        printf("Halide time (CUDA 7.0): %f (ms) GFLOPS: %f \n", t * 1000.0, gflops / t);
    }

    // Benchmark cublas
#ifdef _MSC_VER
    // https://github.com/halide/Halide/issues/5053
    printf("Skipping cublas on Windows; see https://github.com/halide/Halide/issues/5053\n");
#else
    {
        float *A, *B, *C;
        cudaMalloc((void **)&A, size * size * 4);
        cudaMalloc((void **)&B, size * size * 4);
        cudaMalloc((void **)&C, size * size * 4);
        cublasHandle_t handle;
        cublasCreate(&handle);
        float alpha = 1.0f, beta = 1.0f;
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                        size, size, size, &alpha, A, size, B, size, &beta, C, size);
            cudaDeviceSynchronize();
        });
        cudaFree(A);
        cudaFree(B);
        cudaFree(C);
        cublasDestroy(handle);
        printf("cublas time: %f\n", t);
    }
#endif

    printf("Success!\n");
    return 0;
}
