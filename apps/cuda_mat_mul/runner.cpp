#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include "mat_mul.h"
#include "mat_mul_tileir.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

// AOT runners must not pull in Halide.h. We only need the IEEE-754 f16
// bit pattern; the runtime provides halide_float16_bits_to_float for
// converting back to f32 for the reference.
struct halide_f16 {
    uint16_t bits;
    halide_f16() : bits(0) {
    }
    explicit halide_f16(float f) {
        // Round-to-nearest-even f32 → f16. Implemented inline to avoid
        // a libcuda dependency at host side.
        uint32_t u;
        memcpy(&u, &f, 4);
        uint32_t sign = (u >> 16) & 0x8000;
        int32_t exp = ((u >> 23) & 0xff) - 127 + 15;
        uint32_t mant = u & 0x7fffff;
        if (exp >= 31) {
            bits = sign | 0x7c00;
            return;
        }
        if (exp <= 0) {
            if (exp < -10) {
                bits = sign;
                return;
            }
            mant |= 0x800000;
            uint32_t shift = 14 - exp;
            uint32_t round = (mant >> (shift - 1)) & 1;
            bits = sign | ((mant >> shift) + round);
            return;
        }
        uint32_t round = (mant >> 12) & 1;
        bits = sign | (uint32_t(exp) << 10) | (mant >> 13);
        bits += round;
    }
    float to_float() const {
        return halide_float16_bits_to_float(bits);
    }
};

using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

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

    const bool do_correctness = getenv("SKIP_CORRECTNESS") == nullptr;

    // ---- f32 correctness + benchmark (existing CUDA schedule) ----
    if (do_correctness) {
        Buffer<float, 2> A(size, size), B(size, size), C(size, size);
        A.for_each_value([](float &v) { v = (rand() & 3) - 1; });
        B.for_each_value([](float &v) { v = (rand() & 3) - 1; });
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul(A, B, C);
        C.copy_to_host();
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float correct = 0.f;
                for (int k = 0; k < size; k++) {
                    correct += A(x, k) * B(k, y);
                }
                if (correct != C(x, y)) {
                    printf("f32 mismatch %d %d: %f vs %f\n", x, y, correct, C(x, y));
                    return -1;
                }
            }
        }
    }
    {
        Buffer<float, 2> A(size, size), B(size, size), C(size, size);
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            mat_mul(A, B, C);
            C.device_sync();
        });
        double tflops = 2.0 * size * size * size / t / 1e12;
        printf("Halide f32 (CUDA schedule): %f s   %.2f TFLOPS\n", t, tflops);
    }

    // ---- f16 correctness + benchmark (tile-IR schedule) ----
    halide_type_t f16_t{halide_type_float, 16, 1};
    if (do_correctness) {
        Buffer<void, 2> A(f16_t, size, size), B(f16_t, size, size);
        Buffer<float, 2> C(size, size);
        auto fill = [](Buffer<void, 2> &buf) {
            uint16_t *p = (uint16_t *)buf.data();
            for (int i = 0; i < buf.dim(0).extent() * buf.dim(1).extent(); i++) {
                p[i] = halide_f16((float)(rand() & 1)).bits;
            }
        };
        fill(A);
        fill(B);
        A.set_host_dirty();
        B.set_host_dirty();
        mat_mul_tileir(A, B, C);
        C.copy_to_host();
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float correct = 0.f;
                for (int k = 0; k < size; k++) {
                    halide_f16 a, b;
                    a.bits = ((uint16_t *)A.data())[k * size + x];
                    b.bits = ((uint16_t *)B.data())[y * size + k];
                    correct += a.to_float() * b.to_float();
                }
                if (correct != C(x, y)) {
                    printf("tile_ir mismatch %d %d: %f vs %f\n", x, y, correct, C(x, y));
                    return -1;
                }
            }
        }
    }
    {
        Buffer<void, 2> A(f16_t, size, size), B(f16_t, size, size);
        Buffer<float, 2> C(size, size);
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            mat_mul_tileir(A, B, C);
            C.device_sync();
        });
        double tflops = 2.0 * size * size * size / t / 1e12;
        printf("Halide f16 (tile-IR schedule): %f s   %.2f TFLOPS\n", t, tflops);
    }

#ifdef _MSC_VER
    printf("Skipping cublas on Windows; see https://github.com/halide/Halide/issues/5053\n");
#else
    // ---- cublas SGEMM f32 ----
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
        double tflops = 2.0 * size * size * size / t / 1e12;
        printf("cublasSgemm (f32): %f s   %.2f TFLOPS\n", t, tflops);
    }

    // ---- cublasGemmEx f16-in / f32-acc, tensor-core path ----
    {
        __half *A, *B;
        float *C;
        cudaMalloc((void **)&A, size * size * sizeof(__half));
        cudaMalloc((void **)&B, size * size * sizeof(__half));
        cudaMalloc((void **)&C, size * size * sizeof(float));
        cublasHandle_t handle;
        cublasCreate(&handle);
        cublasSetMathMode(handle, CUBLAS_TENSOR_OP_MATH);
        float alpha = 1.0f, beta = 1.0f;
        double t = Halide::Tools::benchmark(5, 5, [&]() {
            cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                         size, size, size,
                         &alpha,
                         A, CUDA_R_16F, size,
                         B, CUDA_R_16F, size,
                         &beta,
                         C, CUDA_R_32F, size,
                         CUBLAS_COMPUTE_32F,
                         CUBLAS_GEMM_DEFAULT_TENSOR_OP);
            cudaDeviceSynchronize();
        });
        cudaFree(A);
        cudaFree(B);
        cudaFree(C);
        cublasDestroy(handle);
        double tflops = 2.0 * size * size * size / t / 1e12;
        printf("cublasGemmEx (f16-in/f32-acc, TC): %f s   %.2f TFLOPS\n", t, tflops);
    }
#endif

    printf("Success!\n");
    return 0;
}
