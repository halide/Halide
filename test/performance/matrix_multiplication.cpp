#include "Halide.h"
#include <cstdio>
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

void simple_version(float* A, float *B, float *C, int width, int stride) {
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
    const int matrix_size = 992, block_size = 32;

    ImageParam A(type_of<float>(), 2);
    ImageParam B(type_of<float>(), 2);

    Var x("x"), xi("xi"), xo("xo"), y("y"), yo("yo"), yi("yo"), yii("yii"), xii("xii");
    Func matrix_mul("matrix_mul");

    RDom k(0, matrix_size);
    RVar ki;

    matrix_mul(x, y) = 0.0f;
    matrix_mul(x, y) += A(k, y) * B(x, k);

    matrix_mul.vectorize(x, 8);

    matrix_mul.update(0)
        .split(x, x, xi, block_size).split(xi, xi, xii, 8)
        .split(y, y, yi, block_size).split(yi, yi, yii, 4)
        .split(k, k, ki, block_size)
        .reorder(xii, yii, xi, ki, yi, k, x, y)
        .parallel(y).vectorize(xii).unroll(xi).unroll(yii);

    matrix_mul
        .bound(x, 0, matrix_size)
        .bound(y, 0, matrix_size);

    matrix_mul.compile_jit();

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

    matrix_mul.realize(output);

    double t = benchmark([&]() {
        matrix_mul.realize(output);
    });

    // check results
    Buffer<float> output_ref(matrix_size, matrix_size);
    Buffer<float> output_halide(matrix_size, matrix_size);

    simple_version(mat_A.data(), mat_B.data(), output_ref.data(), mat_A.width(), mat_A.stride(1));
    matrix_mul.realize(output_halide);

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
        Target t = get_jit_target_from_environment();
        t.set_feature(Target::NoAsserts);
        matrix_mul.compile_to_assembly("/dev/stdout", matrix_mul.infer_arguments(), t);
    }
    */

    float gflops = 2.0f * matrix_size * matrix_size * matrix_size / 1e9f;

    printf("Halide: %fms, %f GFLOP/s\n\n", t * 1e3, (gflops / t));

    printf("Success!\n");
    return 0;
}
