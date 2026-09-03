// TODO: this links libHalide purely to get Halide::float16_t, the same way
// apps/cuda_mat_mul does, and for the same reason: Float16.h is not
// distributed and its bodies live in libHalide.
#include "Halide.h"

#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "../support/bench_harness.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cublas_v2.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <vector>

#include "attention.h"
#include "attention_flash.h"
#include "attention_flash_rdom.h"
#include "attention_softmax.h"

using Halide::float16_t;
using Halide::Runtime::Buffer;

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

// Every form here computes softmax(Q.K' * scale).V with torch's default
// scale, so that the rows compare against torch's kernels like for like.
const float scale = 1.0f / std::sqrt((float)depth);

// The fused filter that is not flash holds a block's worth of scores for every
// key in registers, so past this many keys the launch asks for more registers
// than a thread has and is rejected. It is skipped there rather than run.
constexpr int fused_max_keys = 128;

// The flash filter warms its walk up by rewinding, so it reads whole key steps
// before the first one. It asks for K and V with room in front of them rather
// than clamping the index, which would cost it a constant fold slot. What is in
// there only has to be finite - the step that reads it rescales an accumulator
// that is still zero - but it is filled with zeros so that a run is repeatable.
#ifndef FLASH_CHUNK
#define FLASH_CHUNK 0
#endif
constexpr int flash_chunk = FLASH_CHUNK ? FLASH_CHUNK : (keys / 2 < 64 ? keys / 2 : 64);
constexpr int key_pad = 2 * flash_chunk;

// The two multiplies, and only those: an exponential per score, the two
// reductions along each row and the divide are all real work that this does
// not count. It is the usual way attention is reported, and it compares like
// with like here because every row below has the same numerator and the same
// answer to produce, so the ratios between them are ratios of time. What it is
// not is a fraction of what the hardware can do. The time is printed alongside
// for anyone who wants a number with no convention in it.
double gflops(double seconds) {
    double flops = 2.0 * queries * keys * depth + 2.0 * queries * out_depth * keys;
    return flops / seconds * 1e-9;
}

// Time one call: the shared harness's batched form, launches then one sync
// per trial. `sync` has to match the launcher: Halide runs on its own CUDA
// context.
template<typename F, typename S>
double bench(F &&launch, S &&sync) {
    return hb::bench_s(launch, 10, sync);
}

// Standard normal samples from a fixed seed: an LCG driving Box-Muller. The
// inputs are what a real Q, K and V look like - torch_bench.py draws its own
// the same way - so the scores, the exponentials and the multiplies see
// values of realistic magnitude and bit pattern.
void fill(Buffer<float16_t, 2> &b, uint64_t seed) {
    uint64_t state = seed * 6364136223846793005ull + 1442695040888963407ull;
    auto uniform = [&]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        // The top bits, into (0, 1]: never zero, so the log below is finite.
        return ((state >> 40) + 1) * (1.0f / 16777216.0f);
    };
    b.for_each_value([&](float16_t &v) {
        float u1 = uniform(), u2 = uniform();
        v = float16_t(std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2));
    });
    // for_each_value writes through the host pointer without saying so.
    b.set_host_dirty();
}

// Attention on the host in double precision, for a sample of rows, against an
// output that every form stores in half precision. The tolerance is what the
// forms' own rounding allows for. Rounding to nearest half precision is off by
// at most 2^-11 of the value, which covers the final narrowing of the output.
// The weights go through a half precision operand on the way into the second
// multiply - torch's kernels do the same - which puts up to that same 2^-11
// of noise on each term of the weighted sum. Those errors have random sign,
// so their sum is a random walk whose standard deviation is at most 2^-11
// times the root of the sum of squares of the terms; the tolerance allows
// eight of those.
template<typename T>
bool check(Buffer<float16_t, 2> &Q, Buffer<float16_t, 2> &K,
           Buffer<float16_t, 2> &V, Buffer<T, 2> &O, const char *name) {
    if (getenv("HL_SKIP_CHECK")) {
        return true;
    }
    std::vector<double> weight(keys);
    double worst = 0;
    // A stride coprime with the rows per block, so the samples land at varying
    // offsets within a block.
    for (int y = 0; y < queries; y += 397) {
        double row_max = -1e30;
        for (int j = 0; j < keys; j++) {
            double score = 0;
            for (int i = 0; i < depth; i++) {
                score += (double)Q(i, y) * (double)K(i, j);
            }
            weight[j] = score * scale;
            row_max = std::max(row_max, weight[j]);
        }
        double total = 0;
        for (int j = 0; j < keys; j++) {
            weight[j] = std::exp(weight[j] - row_max);
            total += weight[j];
        }
        for (int x = 0; x < out_depth; x++) {
            double correct = 0, squares = 0;
            for (int j = 0; j < keys; j++) {
                double term = weight[j] * (double)V(x, j);
                correct += term;
                squares += term * term;
            }
            correct /= total;
            const double rounding = 1.0 / 2048;
            double tol = rounding * std::abs(correct) + 8 * rounding * std::sqrt(squares) / total;
            double err = std::abs((double)O(x, y) - correct);
            worst = std::max(worst, err / tol);
            if (err > tol) {
                printf("%s: bad result at %d %d: %f != %f (tolerance %g)\n", name, x, y,
                       (double)O(x, y), correct, tol);
                return false;
            }
        }
    }
    printf("  %s: checked, worst error %.2f of tolerance\n", name, worst);
    return true;
}

// The default handler aborts, which would take the rest of the comparison down
// with whichever filter failed. Holding a block's worth of scores in registers
// is what bounds how many keys the fused filter can do, so it not launching at
// a large key count is a result rather than a crash, and the row that walks the
// keys instead still has a number to print.
extern "C" void halide_error(void *user_context, const char *msg) {
    printf("  %s\n", msg);
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
    Buffer<float16_t, 2> O(out_depth, queries);
    fill(Q, 1);
    fill(K, 2);
    fill(V, 3);

    // The flash filter wants room in front of the keys, so give it its own
    // copies. The other two take K and V as they are.
    Buffer<float16_t, 2> KP(depth, keys + key_pad), VP(out_depth, keys + key_pad);
    KP.translate(1, -key_pad);
    VP.translate(1, -key_pad);
    KP.fill(float16_t(0.f));
    VP.fill(float16_t(0.f));
    for (int j = 0; j < keys; j++) {
        for (int i = 0; i < depth; i++) {
            KP(i, j) = K(i, j);
        }
        for (int i = 0; i < out_depth; i++) {
            VP(i, j) = V(i, j);
        }
    }
    KP.set_host_dirty();
    VP.set_host_dirty();

    int failures = 0;
    if (keys > fused_max_keys) {
        printf("  Halide fused attention        skipped: holds every key's scores in "
               "registers, and does not launch past %d keys\n",
               fused_max_keys);
    } else if (attention(Q.raw_buffer(), K.raw_buffer(), V.raw_buffer(), O.raw_buffer()) != 0) {
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

    // The same attention again, with the keys walked in tiles and the softmax
    // rescaled as it goes.
    // Its own output buffer, so that a filter that failed to write cannot be
    // checked against what the one before it left behind.
    Buffer<float16_t, 2> OF(out_depth, queries);
    if (attention_flash(Q.raw_buffer(), KP.raw_buffer(), VP.raw_buffer(),
                        OF.raw_buffer()) != 0) {
        printf("flash filter returned an error\n");
        failures++;
    } else {
        OF.copy_to_host();
        if (check(Q, K, V, OF, "attention_flash")) {
            double t = bench(
                [&]() {
                    attention_flash(Q.raw_buffer(), KP.raw_buffer(),
                                    VP.raw_buffer(), OF.raw_buffer());
                },
                [&]() { OF.device_sync(); });
            printf("  Halide flash attention        %9.0f GFlop/s %8.1f us\n",
                   gflops(t), t * 1e6);
        } else {
            failures++;
        }
    }

    // The flash walk with no inductive Funcs: the same online softmax, with
    // the running maximum, the row sum and the accumulator carried as one
    // Tuple that a single update over the key chunks advances. It never reads
    // before the first key, so it takes the unpadded panels.
    Buffer<float16_t, 2> OR(out_depth, queries);
    if (attention_flash_rdom(Q.raw_buffer(), K.raw_buffer(), V.raw_buffer(),
                             OR.raw_buffer()) != 0) {
        printf("flash rdom filter returned an error\n");
        failures++;
    } else {
        OR.copy_to_host();
        if (check(Q, K, V, OR, "attention_flash_rdom")) {
            double t = bench(
                [&]() {
                    attention_flash_rdom(Q.raw_buffer(), K.raw_buffer(),
                                         V.raw_buffer(), OR.raw_buffer());
                },
                [&]() { OR.device_sync(); });
            printf("  Halide flash attention (rdom) %9.0f GFlop/s %8.1f us\n",
                   gflops(t), t * 1e6);
        } else {
            failures++;
        }
    }

    // The same attention, unfused: cublas multiplies into a scores matrix in
    // global memory, with the softmax scale applied as its alpha, a kernel
    // normalises it there, and cublas multiplies again into a half precision
    // output. Same arithmetic, same answer - the only difference is that the
    // scores are written out and read back rather than staying in registers.
    cublasCreate(&handle);
    Buffer<float, 2> S(keys, queries);
    Buffer<float16_t, 2> P(keys, queries);
    S.device_malloc(halide_cuda_device_interface());
    P.device_malloc(halide_cuda_device_interface());
    // Its own output buffer, for the same reason the filters above have theirs.
    Buffer<float16_t, 2> OU(out_depth, queries);
    OU.device_malloc(halide_cuda_device_interface());
    void *Qd = (void *)halide_cuda_get_device_ptr(nullptr, Q.raw_buffer());
    void *Kd = (void *)halide_cuda_get_device_ptr(nullptr, K.raw_buffer());
    void *Vd = (void *)halide_cuda_get_device_ptr(nullptr, V.raw_buffer());
    void *Od = (void *)halide_cuda_get_device_ptr(nullptr, OU.raw_buffer());
    void *Sd = (void *)halide_cuda_get_device_ptr(nullptr, S.raw_buffer());
    void *Pd = (void *)halide_cuda_get_device_ptr(nullptr, P.raw_buffer());
    static float alpha = scale, one = 1.0f, beta = 0.0f;

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
    // whatever feeds it has to be half precision too. It accumulates in single
    // precision and narrows on the store, as the filters do.
    auto gemm_out = [&](void *lhs) {
        return ok(cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, out_depth,
                               queries, keys, &one, Vd, CUDA_R_16F, out_depth,
                               lhs, CUDA_R_16F, keys, &beta, Od, CUDA_R_16F,
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
    OU.device_sync();
    OU.set_device_dirty();
    OU.copy_to_host();
    if (!cublas_ok || !check(Q, K, V, OU, "unfused")) {
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
