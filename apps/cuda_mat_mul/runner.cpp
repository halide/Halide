// TODO: this links libHalide purely to get Halide::float16_t and
// Halide::bfloat16_t, which an AOT program should not have to do. Float16.h
// depends on nothing but HalideRuntime.h and already declares halide_type_of
// for both types, but its bodies live in Float16.cpp inside libHalide, and the
// header is not distributed. Making those definitions inline and shipping the
// header, or moving the types to the runtime, would let this go back to
// linking only the generated code.
#include "Halide.h"

#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <cublas_v2.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include "mat_mul.h"
#include "mat_mul_bf16.h"
#include "mat_mul_f16.h"
#include "mat_mul_f16_acc16.h"
#include "mat_mul_u8.h"

using Halide::bfloat16_t;
using Halide::float16_t;
using Halide::Runtime::Buffer;

namespace {

// The same matrix multiply is compiled from one generator at each pair of
// operand and accumulator types, and each is compared against cublas doing the
// same thing. Both are handed the same device buffers - the Halide side fills
// them and copies them down, and cublas is given the pointers out of them - so
// the two see identical data, and both answers are checked.
//
// The operands are small integers so that every dot product is exact in every
// accumulator here, which lets the results be checked for equality rather than
// to a tolerance. That makes them unusually compressible, so the numbers were
// checked against dense random operands too: no configuration moved by more
// than a couple of percent, which is what you would expect of a multiply that
// is issue-bound rather than waiting on memory.

// Time one call. Both implementations queue work asynchronously, and cublas
// does a heuristic lookup on the host for every call, so synchronizing per
// launch would measure that host work rather than letting it overlap with the
// GPU. Batch the launches instead, and sync once. `sync` has to match the
// launcher: Halide runs on its own CUDA context, so cudaDeviceSynchronize does
// not wait for it.
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

double gflops(int size, double seconds) {
    return 2.0 * size * size * size / seconds * 1e-9;
}

template<typename T>
void fill_ints(Buffer<T, 2> &b, int modulus) {
    b.for_each_value([&](T &v) { v = (T)(rand() % modulus); });
    b.set_host_dirty();
}

// Spot check on strides coprime with the tile sizes, so the samples land at
// varying offsets within a tile.
template<typename A, typename C>
bool check(Buffer<A, 2> &Ab, Buffer<A, 2> &Bb, Buffer<C, 2> &Cb,
           int size, const char *name) {
    for (int y = 0; y < size; y += 97) {
        for (int x = 0; x < size; x += 89) {
            double correct = 0;
            for (int k = 0; k < size; k++) {
                correct += (double)Ab(x, k) * (double)Bb(k, y);
            }
            if ((double)Cb(x, y) != correct) {
                printf("%s: bad result at %d %d: %f != %f\n",
                       name, x, y, (double)Cb(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

// One row of the table: run the Halide filter, check it, time it, then time
// cublas on the same device buffers.
template<typename A, typename C, typename Filter, typename Cublas>
bool row(const char *name, int size, int modulus, Filter filter, Cublas cublas) {
    Buffer<A, 2> Ab(size, size), Bb(size, size);
    Buffer<C, 2> Cb(size, size);
    fill_ints(Ab, modulus);
    fill_ints(Bb, modulus);

    if (filter(Ab.raw_buffer(), Bb.raw_buffer(), Cb.raw_buffer()) != 0) {
        printf("%s: filter returned an error\n", name);
        return false;
    }
    Cb.copy_to_host();
    if (!check(Ab, Bb, Cb, size, name)) {
        return false;
    }
    double t = bench([&]() { filter(Ab.raw_buffer(), Bb.raw_buffer(), Cb.raw_buffer()); },
                     [&]() { Cb.device_sync(); });
    printf("  Halide %-12s %9.0f GFlop/s\n", name, gflops(size, t));

    // Running the filter left everything on the device, so cublas gets the
    // same buffers, output included. Halide's dense-first-dimension layout is
    // what cublas calls column major with a leading dimension of the size, so
    // no transposing is needed, and the answer is checked the same way to
    // confirm that.
    void *Ad = (void *)halide_cuda_get_device_ptr(nullptr, Ab.raw_buffer());
    void *Bd = (void *)halide_cuda_get_device_ptr(nullptr, Bb.raw_buffer());
    void *Cd = (void *)halide_cuda_get_device_ptr(nullptr, Cb.raw_buffer());
    cublas(Ad, Bd, Cd, size);
    cudaDeviceSynchronize();
    Cb.set_device_dirty();
    Cb.copy_to_host();
    if (!check(Ab, Bb, Cb, size, name)) {
        printf("  (that was cublas, not Halide)\n");
        return false;
    }
    t = bench([&]() { cublas(Ad, Bd, Cd, size); },
              []() { cudaDeviceSynchronize(); });
    printf("  cublas %-12s %9.0f GFlop/s\n", name, gflops(size, t));
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
    int ver = major * 10 + minor;
    if (ver < 50) {
        printf("[SKIP] This system supports only Cuda compute capability %d.%d, but compute capability 5.0+ is required.\n", major, minor);
        return 0;
    }

    int size = 1024;
    if (argc > 1) {
        size = atoi(argv[1]);
    }

    cublasCreate(&handle);
    static float alpha = 1.0f, beta = 0.0f;
    int failures = 0;

    // A gemm at one pair of types, given how cublas should read the buffers.
    auto gemm_ex = [](cudaDataType at, cudaDataType ct, cublasComputeType_t comp,
                      const void *al, const void *be) {
        return [=](void *A, void *B, void *C, int n) {
            cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, al,
                         A, at, n, B, at, n, be, C, ct, n, comp,
                         CUBLAS_GEMM_DEFAULT);
        };
    };

    failures += !row<float, float>(
        "f32 -> f32", size, 4,
        [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
            return mat_mul(A, B, C);
        },
        [](void *A, void *B, void *C, int n) {
            cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha,
                        (const float *)A, n, (const float *)B, n, &beta,
                        (float *)C, n);
        });

    if (ver < 70) {
        printf("[SKIP] Tensor cores require compute capability 7.0 or above; "
               "this system has %d.%d.\n",
               major, minor);
    } else {
        failures += !row<float16_t, float>(
            "f16 -> f32", size, 4,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_f16(A, B, C);
            },
            gemm_ex(CUDA_R_16F, CUDA_R_32F, CUBLAS_COMPUTE_32F, &alpha, &beta));

        failures += !row<bfloat16_t, float>(
            "bf16 -> f32", size, 4,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_bf16(A, B, C);
            },
            gemm_ex(CUDA_R_16BF, CUDA_R_32F, CUBLAS_COMPUTE_32F, &alpha, &beta));

        // Zeros and ones, so that the dot products stay under 2048, the
        // largest integer half precision represents exactly.
        static __half halpha = __float2half(1.f), hbeta = __float2half(0.f);
        failures += !row<float16_t, float16_t>(
            "f16 -> f16", size, 2,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_f16_acc16(A, B, C);
            },
            gemm_ex(CUDA_R_16F, CUDA_R_16F, CUBLAS_COMPUTE_16F, &halpha, &hbeta));

        // cublas takes signed bytes where the Halide variant takes unsigned
        // ones. The hardware runs both at the same rate, and these values are
        // small enough to mean the same thing either way.
        static int32_t ialpha = 1, ibeta = 0;
        failures += !row<uint8_t, int32_t>(
            "u8 -> i32", size, 4,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_u8(A, B, C);
            },
            gemm_ex(CUDA_R_8I, CUDA_R_32I, CUBLAS_COMPUTE_32I, &ialpha, &ibeta));
    }

    cublasDestroy(handle);
    if (failures) {
        printf("%d configuration(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
