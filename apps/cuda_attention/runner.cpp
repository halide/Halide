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
#include <cuda.h>
#include <cuda_runtime.h>
#include <vector>

#include "attention.h"
#include "attention_softmax.h"

using Halide::float16_t;
using Halide::Runtime::Buffer;

// The filter accumulates its second multiply in whatever its output type is,
// so the two are chosen together, by the generator param the Makefile passes.
#ifdef OUT_HALF
using out_t = float16_t;
constexpr cudaDataType out_cuda_type = CUDA_R_16F;
#else
using out_t = float;
constexpr cudaDataType out_cuda_type = CUDA_R_32F;
#endif

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

// The two multiplies, and only those: an exponential per score, the two
// reductions along each row and the divide are all real work that this does
// not count. It is the usual way attention is reported, and it compares like
// with like here because every row below has the same numerator and the same
// answer to produce, so the ratios between them are ratios of time. What it is
// not is a fraction of what the hardware can do, and the row that skips the
// softmax gets to keep the same numerator for less work, which is why it reads
// high. The time is printed alongside for anyone who wants a number with no
// convention in it.
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
           Buffer<float16_t, 2> &V, Buffer<out_t, 2> &O, const char *name) {
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
            // A half precision accumulator carries about three decimal
            // digits, so it needs a looser tolerance than a single precision
            // one does.
            const float tol = sizeof(out_t) == 2 ? 2e-2f : 2e-3f;
            if (std::abs((float)O(x, y) - correct) > tol * std::abs(correct) + 1e-5f) {
                printf("%s: bad result at %d %d: %f != %f\n", name, x, y,
                       (double)O(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

cublasHandle_t handle;

// Halide makes a CUDA context of its own unless it is given one. Handing it
// the one the runtime API and cublas use puts every kernel on the same
// timeline, so one synchronize covers all of them and events recorded between
// them mean something.
void *shared_context = nullptr;

int acquire_context(void *user_context, void **ctx, bool create) {
    *ctx = shared_context;
    return 0;
}

int release_context(void *user_context) {
    return 0;
}

// cublas reports an unsupported combination by returning an error rather than
// by failing to run, and a call that did nothing still synchronizes cleanly,
// so every call has to be checked or it can quietly be timed as free.
bool ok(cublasStatus_t status, const char *what) {
    if (status != CUBLAS_STATUS_SUCCESS) {
        printf("%s: cublas returned %d\n", what, (int)status);
        return false;
    }
    return true;
}

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

    // Make the runtime API bring up its primary context, then give it to
    // Halide before anything is allocated on either side.
    cudaFree(nullptr);
    if (cuCtxGetCurrent((CUcontext *)&shared_context) != CUDA_SUCCESS || !shared_context) {
        printf("could not get the CUDA context\n");
        return 1;
    }
    halide_set_cuda_acquire_context(acquire_context);
    halide_set_cuda_release_context(release_context);

    Buffer<float16_t, 2> Q(depth, queries), K(depth, keys), V(out_depth, keys);
    Buffer<out_t, 2> O(out_depth, queries);
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
            printf("  Halide fused attention        %9.0f GFlop/s %8.1f us\n",
                   gflops(t), t * 1e6);
        } else {
            failures++;
        }
    }

    // The same attention, unfused: cublas multiplies into a scores matrix in
    // global memory, a kernel normalises it there, and cublas multiplies
    // again. Same arithmetic, same answer - the only difference is that the
    // scores are written out and read back rather than staying in registers.
    cublasCreate(&handle);
    Buffer<float, 2> S(keys, queries);
    Buffer<float16_t, 2> P(keys, queries);
    S.device_malloc(halide_cuda_device_interface());
    P.device_malloc(halide_cuda_device_interface());
    void *Qd = (void *)halide_cuda_get_device_ptr(nullptr, Q.raw_buffer());
    void *Kd = (void *)halide_cuda_get_device_ptr(nullptr, K.raw_buffer());
    void *Vd = (void *)halide_cuda_get_device_ptr(nullptr, V.raw_buffer());
    void *Od = (void *)halide_cuda_get_device_ptr(nullptr, O.raw_buffer());
    void *Sd = (void *)halide_cuda_get_device_ptr(nullptr, S.raw_buffer());
    void *Pd = (void *)halide_cuda_get_device_ptr(nullptr, P.raw_buffer());
    static float alpha = 1.0f, beta = 0.0f;

    // Halide's buffers are dense in their first dimension, which is what
    // cublas calls column major, so neither multiply needs a transpose beyond
    // the one attention itself asks for.
    auto gemm_scores = [&](void *dst, cudaDataType dst_type) {
        return ok(cublasGemmEx(handle, CUBLAS_OP_T, CUBLAS_OP_N, keys, queries,
                               depth, &alpha, Kd, CUDA_R_16F, depth, Qd,
                               CUDA_R_16F, depth, &beta, dst, dst_type, keys,
                               CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT),
                  "scores");
    };
    // The multiply against V takes both its operands in half precision, so
    // whatever feeds it has to be half precision too.
    auto gemm_out = [&](void *lhs) {
        return ok(cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, out_depth,
                               queries, keys, &alpha, Vd, CUDA_R_16F, out_depth,
                               lhs, CUDA_R_16F, keys, &beta, Od, out_cuda_type,
                               out_depth, CUBLAS_COMPUTE_32F,
                               CUBLAS_GEMM_DEFAULT),
                  "out");
    };
    bool cublas_ok = true;
    auto unfused = [&]() {
        cublas_ok &= gemm_scores(Sd, CUDA_R_32F);
        S.set_device_dirty();
        attention_softmax(S.raw_buffer(), P.raw_buffer());
        cublas_ok &= gemm_out(Pd);
    };

    unfused();
    O.device_sync();
    O.set_device_dirty();
    O.copy_to_host();
    if (!cublas_ok || !check(Q, K, V, O, "unfused")) {
        failures++;
    } else {
        double t = bench(unfused, []() { cudaDeviceSynchronize(); });
        printf("  cublas + softmax + cublas     %9.0f GFlop/s %8.1f us\n",
               gflops(t), t * 1e6);
        // Where the time goes, measured between the kernels of one pass
        // rather than by running each on its own. Running one on its own in a
        // loop leaves whatever it touches in cache for its next go, which is
        // not the state it runs in here: the softmax reads a scores matrix the
        // multiply before it just wrote, and writes one the multiply after it
        // has yet to read.
        cudaEvent_t ev[4];
        for (auto &e : ev) {
            cudaEventCreate(&e);
        }
        double phase[3] = {0, 0, 0};
        const int passes = 20;
        for (int i = 0; i < passes; i++) {
            cudaEventRecord(ev[0]);
            gemm_scores(Sd, CUDA_R_32F);
            cudaEventRecord(ev[1]);
            S.set_device_dirty();
            attention_softmax(S.raw_buffer(), P.raw_buffer());
            cudaEventRecord(ev[2]);
            gemm_out(Pd);
            cudaEventRecord(ev[3]);
            cudaDeviceSynchronize();
            for (int j = 0; j < 3; j++) {
                float ms = 0;
                cudaEventElapsedTime(&ms, ev[j], ev[j + 1]);
                phase[j] += ms * 1e3 / passes;
            }
        }
        for (auto &e : ev) {
            cudaEventDestroy(e);
        }
        printf("    phases: gemm1 %.1fus  softmax %.1fus  gemm2 %.1fus  (sum %.1f, whole %.1f)\n",
               phase[0], phase[1], phase[2], phase[0] + phase[1] + phase[2], t * 1e6);
    }

    cublasDestroy(handle);

    if (failures) {
        printf("%d configuration(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
