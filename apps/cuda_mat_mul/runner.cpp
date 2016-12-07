#include "cuda_mat_mul.h"
#include "benchmark.h"
#include "HalideBuffer.h"
#include <cublas_v2.h>
#include <cuda_runtime.h>

int main(int argc, char **argv) {
    const int size = 1024;
    {
        Halide::Buffer<float> A(size, size), B(size, size), C(size, size);
        double t = benchmark(10, 10, [&]() {
                                         mat_mul(A, B, C);
                                         C.device_sync();
                                     });
        printf("%f\n", t);
    }

    {
        // Now check cublas
        float *A, *B, *C;
        cudaMalloc((void **)&A, size*size*4);
        cudaMalloc((void **)&B, size*size*4);
        cudaMalloc((void **)&C, size*size*4);
        cublasHandle_t handle;
        cublasCreate(&handle);
        float alpha = 1.0f, beta = 1.0f;
        double t =
            benchmark(3, 3, [&]() {
                                cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                                            size, size, size, &alpha, A, size, B, size, &beta, C, size);
                                cudaDeviceSynchronize();
                            });
        cudaFree(A);
        cudaFree(B);
        cudaFree(C);
        cublasDestroy(handle);
        printf("%f\n", t);
    }
    return 0;
}
