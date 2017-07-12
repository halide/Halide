#include "bin/mat_mul.h"
#include "halide_benchmark.h"
#include "HalideBuffer.h"
#include <cublas_v2.h>
#include <cuda_runtime.h>

using Halide::Runtime::Buffer;

int main(int argc, char **argv) {
    int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    {
        Buffer<float> A(size, size), B(size, size), C(size, size);
        double t = benchmark(10, 10, [&]() {
            mat_mul(A, B, C);
            C.device_sync();
        });
        printf("Halide time: %f\n", t);
    }

    {
        float *A, *B, *C;
        cudaMalloc((void **)&A, size*size*4);
        cudaMalloc((void **)&B, size*size*4);
        cudaMalloc((void **)&C, size*size*4);
        cublasHandle_t handle;
        cublasCreate(&handle);
        float alpha = 1.0f, beta = 1.0f;
        double t = benchmark(10, 10, [&]() {
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
    return 0;
}
