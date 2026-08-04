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

// The same matrix multiply is compiled from one generator at each pair of
// operand and accumulator types, and each is compared against cublas doing the
// same thing. Both are handed the same device buffers - the Halide side fills
// them and copies them down, and cublas is given the pointers out of them - so
// the two see identical data.
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

// Read or write an element of a buffer of any of the types used here, so that
// the ones with no C++ equivalent are handled too.
double element(const halide_buffer_t *b, size_t i) {
    halide_type_t t = b->type;
    if (t == halide_type_t(halide_type_float, 16)) {
        return (double)((const _Float16 *)b->host)[i];
    } else if (t == halide_type_t(halide_type_bfloat, 16)) {
        // A bfloat is the top half of a float.
        uint32_t bits = (uint32_t)((const uint16_t *)b->host)[i] << 16;
        float f;
        memcpy(&f, &bits, 4);
        return f;
    } else if (t == halide_type_t(halide_type_float, 32)) {
        return ((const float *)b->host)[i];
    } else if (t == halide_type_t(halide_type_uint, 8)) {
        return ((const uint8_t *)b->host)[i];
    } else if (t == halide_type_t(halide_type_int, 32)) {
        return ((const int32_t *)b->host)[i];
    }
    fprintf(stderr, "unhandled buffer type\n");
    exit(1);
}

void set_element(halide_buffer_t *b, size_t i, int v) {
    halide_type_t t = b->type;
    if (t == halide_type_t(halide_type_float, 16)) {
        ((_Float16 *)b->host)[i] = (_Float16)v;
    } else if (t == halide_type_t(halide_type_bfloat, 16)) {
        float f = (float)v;
        uint32_t bits;
        memcpy(&bits, &f, 4);
        ((uint16_t *)b->host)[i] = (uint16_t)(bits >> 16);
    } else if (t == halide_type_t(halide_type_float, 32)) {
        ((float *)b->host)[i] = (float)v;
    } else if (t == halide_type_t(halide_type_uint, 8)) {
        ((uint8_t *)b->host)[i] = (uint8_t)v;
    } else {
        fprintf(stderr, "unhandled operand type\n");
        exit(1);
    }
}

void fill_ints(Buffer<void, 2> &b, int modulus) {
    size_t n = (size_t)b.width() * b.height();
    for (size_t i = 0; i < n; i++) {
        set_element(b.raw_buffer(), i, rand() % modulus);
    }
    b.set_host_dirty();
}

// Spot check on strides coprime with the tile sizes, so the samples land at
// varying offsets within a tile.
bool check(Buffer<void, 2> &Ab, Buffer<void, 2> &Bb, Buffer<void, 2> &Cb,
           int size, const char *name) {
    for (int y = 0; y < size; y += 97) {
        for (int x = 0; x < size; x += 89) {
            double correct = 0;
            for (int k = 0; k < size; k++) {
                correct += element(Ab.raw_buffer(), (size_t)k * size + x) *
                           element(Bb.raw_buffer(), (size_t)y * size + k);
            }
            double got = element(Cb.raw_buffer(), (size_t)y * size + x);
            if (got != correct) {
                printf("%s: bad result at %d %d: %f != %f\n",
                       name, x, y, got, correct);
                return false;
            }
        }
    }
    return true;
}

// One row of the table: run the Halide filter, check it, time it, then time
// cublas on the same device buffers.
template<typename Filter, typename Cublas>
bool row(const char *name, int size, halide_type_t a_type, halide_type_t c_type,
         int modulus, Filter filter, Cublas cublas) {
    Buffer<void, 2> Ab(a_type, size, size), Bb(a_type, size, size);
    Buffer<void, 2> Cb(c_type, size, size), Cb_cublas(c_type, size, size);
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

    // Running the filter left the operands on the device. Hand cublas the
    // same ones, and somewhere of its own to put an answer that is only timed.
    Cb_cublas.device_malloc(halide_cuda_device_interface());
    void *Ad = (void *)halide_cuda_get_device_ptr(nullptr, Ab.raw_buffer());
    void *Bd = (void *)halide_cuda_get_device_ptr(nullptr, Bb.raw_buffer());
    void *Cd = (void *)halide_cuda_get_device_ptr(nullptr, Cb_cublas.raw_buffer());
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
    static float alpha = 1.0f, beta = 1.0f;
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

    failures += !row(
        "f32 -> f32", size, halide_type_t(halide_type_float, 32),
        halide_type_t(halide_type_float, 32), 4,
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
        failures += !row(
            "f16 -> f32", size, halide_type_t(halide_type_float, 16),
            halide_type_t(halide_type_float, 32), 4,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_f16(A, B, C);
            },
            gemm_ex(CUDA_R_16F, CUDA_R_32F, CUBLAS_COMPUTE_32F, &alpha, &beta));

        failures += !row(
            "bf16 -> f32", size, halide_type_t(halide_type_bfloat, 16),
            halide_type_t(halide_type_float, 32), 4,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_bf16(A, B, C);
            },
            gemm_ex(CUDA_R_16BF, CUDA_R_32F, CUBLAS_COMPUTE_32F, &alpha, &beta));

        // Zeros and ones, so that the dot products stay under 2048, the
        // largest integer half precision represents exactly.
        static __half halpha = __float2half(1.f), hbeta = __float2half(1.f);
        failures += !row(
            "f16 -> f16", size, halide_type_t(halide_type_float, 16),
            halide_type_t(halide_type_float, 16), 2,
            [](halide_buffer_t *A, halide_buffer_t *B, halide_buffer_t *C) {
                return mat_mul_f16_acc16(A, B, C);
            },
            gemm_ex(CUDA_R_16F, CUDA_R_16F, CUBLAS_COMPUTE_16F, &halpha, &hbeta));

        // cublas takes signed bytes where the Halide variant takes unsigned
        // ones. The hardware runs both at the same rate, and these values are
        // small enough to mean the same thing either way.
        static int32_t ialpha = 1, ibeta = 1;
        failures += !row(
            "u8 -> i32", size, halide_type_t(halide_type_uint, 8),
            halide_type_t(halide_type_int, 32), 4,
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
