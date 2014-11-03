#include <Halide.h>
#include <stdio.h>
#include "clock.h"

using namespace Halide;

void simple_version(float* A, float *B, float *C, int width, int stride) {
    for(int iy = 0; iy < width; iy++) {
        for(int ix = 0; ix < width; ix++) {
            float *cc = C + iy * stride + ix;
            *cc = 0.0f;

            for(int ik = 0; ik < width; ik++) {
                *cc = *cc + A[iy * stride + ik] * B[ik * stride + ix];
            }
        }
    }
}


int main(int argc, char **argv) {
    const int width = 992, block_size = 32;

    ImageParam A(type_of<float>(), 2);
    ImageParam B(type_of<float>(), 2);

    Var x("x"), xi("xi"), xo("xo"), y("y"), yo("yo"), yi("yo");
    Func matrix_mul("matrix_mul");

    RDom k(0, width);
    RVar ki;

    matrix_mul(x, y) = 0.0f;
    matrix_mul(x, y) += A(k, y) * B(x, k);

    matrix_mul.vectorize(x, 4);

    matrix_mul.update(0).split(x, x, xi, block_size).split(y, y, yi, block_size).split(k, k, ki, block_size).reorder(xi, ki, yi, k, x, y).parallel(y).vectorize(xi, 4).unroll(xi, 4);

    matrix_mul.bound(x, 0, width);
    matrix_mul.bound(y, 0, width);

    matrix_mul.compile_jit();


    const int iterations = 50;

    Image<float> mat_A(width, width);
    Image<float> mat_B(width, width);
    Image<float> output(width, width);

    // init randomly
    for(int iy = 0; iy < width; iy++) {
        for(int ix = 0; ix < width; ix++) {
            mat_A(ix, iy) = (rand() % 256) / 256.0f;
            mat_B(ix, iy) = (rand() % 256) / 256.0f;
        }
    }

    A.set(mat_A);
    B.set(mat_B);

    matrix_mul.realize(output);

    double t = 0;
    for (int i = 0; i < iterations; i++) {
        double t3 = current_time();
        matrix_mul.realize(output);
        double t4 = current_time();
        t += t4-t3;
    }


    // check results
    Image<float> output_ref(width, width);
    Image<float> output_halide(width, width);

    simple_version(mat_A.data(), mat_B.data(), output_ref.data(), mat_A.width(), mat_A.stride(1));
    matrix_mul.realize(output_halide);

    bool halide_correct = true;
    for(int iy = 0; iy < width && halide_correct; iy++) {
        for(int ix = 0; ix < width; ix++) {
            halide_correct = halide_correct && (std::abs(output_ref(ix, iy) - output_halide(ix, iy)) < 0.000001f);
        }
    }

    if(halide_correct)
        printf("Halide results - OK\n");
    else
        printf("Halide results - FAIL\n");

    float flops = 2.0f * width * width * width;

    printf("halide: %fms, %f MFLOP/s\n\n", t, (flops / t) * iterations / 1000);

    printf("Success!\n");
    return 0;
}
