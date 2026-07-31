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
using Halide::Tools::benchmark;

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

        double t = benchmark(5, 5, [&]() {
            mat_mul(A, B, C);
            C.device_sync();
        });
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

        double t = benchmark(5, 5, [&]() {
            mat_mul_f16(A, B, C);
            C.device_sync();
        });
        printf("Halide half (tensor cores): %f s (%.1f GFlop/s)\n",
               t, gflops(size, t));
    }

    // Benchmark cublas at single precision, for reference.
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
        double t = benchmark(5, 5, [&]() {
            cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                        size, size, size, &alpha, A, size, B, size, &beta, C, size);
            cudaDeviceSynchronize();
        });
        cudaFree(A);
        cudaFree(B);
        cudaFree(C);
        cublasDestroy(handle);
        printf("cublas float: %f s (%.1f GFlop/s)\n", t, gflops(size, t));
    }
#endif

    printf("Success!\n");
    return 0;
}
