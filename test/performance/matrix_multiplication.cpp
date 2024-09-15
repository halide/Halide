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

    ImageParam A(type_of<float>(), 2);
    ImageParam B(type_of<float>(), 2);

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

    const int vec = target.natural_vector_size<float>();

    // Size the inner loop tiles to fit into the number of registers available
    // on the target, using either 12 accumulator registers or 24.
    const int inner_tile_x = 3 * vec;
    const int inner_tile_y = (target.has_feature(Target::AVX512) || target.arch != Target::X86) ? 8 : 4;

    // The shape of the outer tiling
    const int tile_y = matrix_size / 4;
    const int tile_k = matrix_size / 16;

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

    // Uncomment to see the generated assembly.
    /*
    {
        Target t("host-no_asserts-no_runtime-no_bounds_query");
        out.compile_to_assembly("/dev/stdout", matrix_mul.infer_arguments(), t);
    }
    */

    float gflops = 2.0f * matrix_size * matrix_size * matrix_size / 1e9f;

    printf("Halide: %fms, %f GFLOP/s\n\n", t * 1e3, (gflops / t));

    printf("Success!\n");
    return 0;
}
