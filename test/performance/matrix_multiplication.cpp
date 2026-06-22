#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

void simple_version(float *A, float *B, float *C, int width, int stride) {
    for (int iy = 0; iy < width; iy++) {
        for (int ix = 0; ix < width; ix++) {
            float *cc = C + iy * stride + ix;
            *cc = 0.0f;

            for (int ik = 0; ik < width; ik++) {
                *cc = *cc + A[iy * stride + ik] * B[ik * stride + ix];
            }
        }
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    const int matrix_size = 992;

    ImageParam A(type_of<float>(), 2, "A");
    ImageParam B(type_of<float>(), 2, "B");

    Var x("x"), y("y");
    RDom k(0, matrix_size);

    Func matrix_mul("matrix_mul");

    matrix_mul(x, y) += A(k, y) * B(x, k);

    Func out;
    out(x, y) = matrix_mul(x, y);

    // Now the schedule. Single-threaded, it hits 155 GFlops on Skylake-X
    // i9-9960x with AVX-512 (80% of peak), and 87 GFlops with AVX2 (90% of
    // peak).
    //
    // Using 16 threads (and no hyperthreading), hits 2080 GFlops (67% of peak)
    // and 1310 GFLops (85% of peak) respectively.
    //
    // On Apple M3 Max, single-threaded hits ~114 GFlops (89% of peak), and
    // ~1270 GFlops using 16 cores.

    const int vec = target.natural_vector_size<float>();

    // On 64-bit ARM, there are 32 NEON registers. Using inner_tile_x=4*vec
    // with inner_tile_y=4 leaves 10 spare NEON registers, which lets LLVM
    // assign an independent GP base address to each A row. This avoids the
    // ld1r post-increment serial dependency chain that occurs with 8 rows
    // (where only 2 temp registers cycle between rows), and produces balanced
    // load/compute throughput (4 cycles each at 4 FP units and 2 load ports).
    const bool is_aarch64 = target.arch == Target::ARM && target.bits == 64;
    const bool is_avx512 = target.has_feature(Target::AVX512);

    // Size the inner loop tiles to fit into the number of registers available
    // on the target.
    // ARM64 NEON:      4×4=16 accumulators (22/32 NEON regs).
    // AVX-512:         3×8=24 accumulators (27/32 ZMM regs).
    // AVX2 (default):  3×4=12 accumulators.
    const int inner_tile_x = is_aarch64 ? 4 * vec : 3 * vec;
    const int inner_tile_y = is_avx512 ? 8 : 4;

    // The shape of the outer tiling. On ARM64, use a narrower y-tile so the
    // B panel (inner_tile_x × matrix_k × 4 bytes = ~62KB) fits in L1
    // alongside the C accumulator buffer.
    const int tile_y = matrix_size / (is_aarch64 ? 8 : 4);
    const int tile_k = matrix_size / (is_aarch64 ? 4 : 16);

    Var xy("xy"), xi("xi"), yi("yi"), yii("yii");

    out.tile(x, y, xi, yi, inner_tile_x, tile_y)
        .split(yi, yi, yii, inner_tile_y)
        .vectorize(xi, vec)
        .unroll(xi)
        .unroll(yii)
        .fuse(x, y, xy)
        .parallel(xy);

    RVar ko("ko"), ki("ki");
    Var z("z");
    matrix_mul.update().split(k, ko, ki, tile_k);

    // Factor the reduction so that we can do outer blocking over the reduction
    // dimension.
    Func intm = matrix_mul.update().rfactor(ko, z);

    intm.compute_at(matrix_mul, y)
        .vectorize(x, vec)
        .unroll(x)
        .unroll(y);

    intm.update(0)
        .reorder(x, y, ki)
        .vectorize(x, vec)
        .unroll(x)
        .unroll(y);

    matrix_mul.compute_at(out, xy)
        .vectorize(x, vec)
        .unroll(x);

    matrix_mul.update()
        .split(y, y, yi, inner_tile_y)
        .reorder(x, yi, y, ko)
        .vectorize(x, vec)
        .unroll(x)
        .unroll(yi);

    out
        .bound(x, 0, matrix_size)
        .bound(y, 0, matrix_size);

    Buffer<float> mat_A(matrix_size, matrix_size);
    Buffer<float> mat_B(matrix_size, matrix_size);
    Buffer<float> output(matrix_size, matrix_size);

    // init randomly
    for (int iy = 0; iy < matrix_size; iy++) {
        for (int ix = 0; ix < matrix_size; ix++) {
            mat_A(ix, iy) = (rand() % 256) / 256.0f;
            mat_B(ix, iy) = (rand() % 256) / 256.0f;
        }
    }

    A.set(mat_A);
    B.set(mat_B);

    out.realize(output);

    double t = benchmark([&]() {
        out.realize(output);
    });

    // check results
    Buffer<float> output_ref(matrix_size, matrix_size);
    Buffer<float> output_halide(matrix_size, matrix_size);

    simple_version(mat_A.data(), mat_B.data(), output_ref.data(), mat_A.width(), mat_A.stride(1));
    out.realize(output_halide);

    bool halide_correct = true;
    for (int iy = 0; iy < matrix_size && halide_correct; iy++) {
        for (int ix = 0; ix < matrix_size; ix++) {
            halide_correct = halide_correct && (std::abs(output_ref(ix, iy) - output_halide(ix, iy)) < 0.001f);
        }
    }

    if (halide_correct) {
        printf("Halide results - OK\n");
    } else {
        printf("Halide results - FAIL\n");
        return 1;
    }

    float gflops = 2.0f * matrix_size * matrix_size * matrix_size / 1e9f;
    printf("Halide: %fms, %f GFLOP/s\n\n", t * 1e3, (gflops / t));

    printf("Success!\n");
    return 0;
}
