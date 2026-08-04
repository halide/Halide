#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include "mat_mul.h"
#include "mat_mul_bf16.h"
#include "mat_mul_f16.h"
#include "mat_mul_f16_acc16.h"
#include "mat_mul_u8.h"

using Halide::Runtime::Buffer;

namespace {

// The operands are small integers so that every dot product is exact in every
// accumulator here, which lets the results be checked for equality rather than
// to a tolerance. That makes them unusually compressible, so the numbers were
// checked against dense random operands too: no configuration moved by more
// than a couple of percent, which is what you would expect of a multiply that
// is issue-bound rather than waiting on memory.
//
// The same matrix multiply is compiled twice from one generator: once with
// float operands, which get a schedule that accumulates in ordinary registers,
// and once with half operands, which get the tensor cores. Both accumulate in
// and return single precision, so the two are directly comparable.

template<typename T, typename O>
bool check(const Buffer<T, 2> &A, const Buffer<T, 2> &B,
           const Buffer<O, 2> &C, int size, const char *name) {
    // Spot check on strides that are coprime with the tile sizes, so the
    // samples land at varying offsets within a tile.
    for (int y = 0; y < size; y += 97) {
        for (int x = 0; x < size; x += 89) {
            double correct = 0;
            for (int k = 0; k < size; k++) {
                correct += (double)A(x, k) * (double)B(k, y);
            }
            // The operands are small integers, which are exact in both float
            // and half, and the accumulator is single precision either way, so
            // the answer should be exact.
            if ((double)C(x, y) != correct) {
                printf("%s: bad result at %d %d: %f != %f\n",
                       name, x, y, (double)C(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

// There is no C++ type for bfloat16 here, so the buffer carries the type at
// runtime and these convert. A bfloat is the top half of a float, so for the
// small integers this uses the conversion is exact in both directions.
uint16_t to_bf16(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return (uint16_t)(bits >> 16);
}

float from_bf16(uint16_t h) {
    uint32_t bits = (uint32_t)h << 16;
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

double gflops(int size, double seconds) {
    return 2.0 * size * size * size / seconds * 1e-9;
}

// Time one call of the filter. Both implementations queue work
// asynchronously, and cublas does a heuristic lookup on the host for every
// call, so synchronizing per launch would measure that host work rather than
// letting it overlap with the GPU. Batch the launches instead, and sync once.
// `sync` has to match the launcher: Halide runs on its own CUDA context, so
// cudaDeviceSynchronize does not wait for it.
template<typename F, typename S>
double bench_batched(F &&launch, S &&sync) {
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

    // Half precision operands accumulated into half precision, which halves
    // the registers the accumulator needs. The operands here are sparse zeros
    // and ones, so the dot products stay small enough to be exact even in a
    // half accumulator, whose integers run out at 2048.
    if (ver >= 70) {
        Buffer<_Float16, 2> A(size, size), B(size, size);
        Buffer<_Float16, 2> C(size, size);
        A.for_each_value([](_Float16 &v) { v = (_Float16)((rand() & 3) == 0); });
        B.for_each_value([](_Float16 &v) { v = (_Float16)((rand() & 3) == 0); });
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul_f16_acc16(A, B, C);
        C.copy_to_host();
        if (!check(A, B, C, size, "half into half")) {
            return 1;
        }
        double t = bench_batched([&]() { mat_mul_f16_acc16(A, B, C); },
                                 [&]() { C.device_sync(); });
        printf("Halide half into half (tensor cores): %f s (%.1f GFlop/s)\n",
               t, gflops(size, t));
    }

    // The other operand types the tensor cores multiply. Brain floats
    // accumulate into single precision like halves do, and eight-bit integers
    // into 32-bit ones, which is the interesting case for imaging.
    if (ver >= 80) {
        {
            const halide_type_t bf16(halide_type_bfloat, 16);
            Buffer<void, 2> A(bf16, size, size), B(bf16, size, size);
            Buffer<float, 2> C(size, size);
            // The buffer carries its type at runtime, so index the raw
            // storage rather than going through a typed view.
            uint16_t *Ap = (uint16_t *)A.data(), *Bp = (uint16_t *)B.data();
            auto Af = [&](int i, int j) { return from_bf16(Ap[j * size + i]); };
            auto Bf = [&](int i, int j) { return from_bf16(Bp[j * size + i]); };
            for (int i = 0; i < size * size; i++) {
                Ap[i] = to_bf16((float)((rand() & 3) - 1));
                Bp[i] = to_bf16((float)((rand() & 3) - 1));
            }
            A.set_host_dirty();
            B.set_host_dirty();
            mat_mul_bf16(A, B, C);
            C.copy_to_host();
            for (int y = 0; y < size; y += 97) {
                for (int x = 0; x < size; x += 89) {
                    double correct = 0;
                    for (int k = 0; k < size; k++) {
                        correct += (double)Af(x, k) * (double)Bf(k, y);
                    }
                    if ((double)C(x, y) != correct) {
                        printf("bfloat: bad result at %d %d: %f != %f\n",
                               x, y, (double)C(x, y), correct);
                        return 1;
                    }
                }
            }
            double t = bench_batched([&]() { mat_mul_bf16(A, B, C); },
                                     [&]() { C.device_sync(); });
            printf("Halide bfloat (tensor cores): %f s (%.1f GFlop/s)\n",
                   t, gflops(size, t));
        }
        {
            Buffer<uint8_t, 2> A(size, size), B(size, size);
            Buffer<int32_t, 2> C(size, size);
            A.for_each_value([](uint8_t &v) { v = (uint8_t)(rand() & 3); });
            B.for_each_value([](uint8_t &v) { v = (uint8_t)(rand() & 3); });
            A.set_host_dirty();
            B.set_host_dirty();
            mat_mul_u8(A, B, C);
            C.copy_to_host();
            if (!check(A, B, C, size, "uint8")) {
                return 1;
            }
            double t = bench_batched([&]() { mat_mul_u8(A, B, C); },
                                     [&]() { C.device_sync(); });
            printf("Halide uint8 (tensor cores): %f s (%.1f GFlop/s)\n",
                   t, gflops(size, t));
        }
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

            // Brain floats, also into a single precision accumulator.
            t = bench_batched([&]() { cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                                   size, size, size, &alpha,
                                                   A, CUDA_R_16BF, size,
                                                   B, CUDA_R_16BF, size, &beta,
                                                   C, CUDA_R_32F, size,
                                                   CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT); },
                              []() { cudaDeviceSynchronize(); });
            printf("cublas bfloat: %f s (%.1f GFlop/s)\n", t, gflops(size, t));

            // Eight-bit integers into a 32-bit accumulator. cublas takes
            // signed operands here where the Halide variant above takes
            // unsigned ones; the hardware runs both at the same rate.
            int32_t ialpha = 1, ibeta = 1;
            t = bench_batched([&]() { cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                                   size, size, size, &ialpha,
                                                   A, CUDA_R_8I, size,
                                                   B, CUDA_R_8I, size, &ibeta,
                                                   C, CUDA_R_32I, size,
                                                   CUBLAS_COMPUTE_32I, CUBLAS_GEMM_DEFAULT); },
                              []() { cudaDeviceSynchronize(); });
            printf("cublas int8: %f s (%.1f GFlop/s)\n", t, gflops(size, t));

            // Halves into a half accumulator, which is what the half output
            // variant above does.
            __half halpha = __float2half(1.f), hbeta = __float2half(1.f);
            t = bench_batched([&]() { cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                                   size, size, size, &halpha,
                                                   A, CUDA_R_16F, size,
                                                   B, CUDA_R_16F, size, &hbeta,
                                                   C, CUDA_R_16F, size,
                                                   CUBLAS_COMPUTE_16F, CUBLAS_GEMM_DEFAULT); },
                              []() { cudaDeviceSynchronize(); });
            printf("cublas half into half: %f s (%.1f GFlop/s)\n",
                   t, gflops(size, t));
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
