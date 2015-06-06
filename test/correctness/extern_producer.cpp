#include "Halide.h"
#include <stdio.h>

using namespace Halide;


#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// Some helper functions for rounding
int round_down(int, int);
int round_up(int x, int m) {
    if (x < 0) return -round_down(-x, m);
    else return ((x + m - 1) / m) * m;
}

int round_down(int x, int m) {
    if (x < 0) return -round_up(-x, m);
    else return (x / m) * m;
}

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" DLLEXPORT int make_data(buffer_t *out) {
    if (!out->host) {
        // Bounds query mode. To make life interesting, let's add some
        // arbitrary constraints on what we can produce.

        // The start and end of the x coord must be a multiple of 10.
        int max_plus_one = out->min[0] + out->extent[0];
        max_plus_one = round_up(max_plus_one, 10);
        out->min[0] = round_down(out->min[0], 10);
        out->extent[0] = max_plus_one - out->min[0];

        // There must be at least 40 scanlines.
        if (out->extent[1] < 40) {
            out->extent[1] = 40;
        }
        return 0;
    }
    assert(out->host && out->elem_size == 4 && out->stride[0] == 1);
    printf("Generating data over [%d %d] x [%d %d]\n",
           out->min[0], out->min[0] + out->extent[0],
           out->min[1], out->min[1] + out->extent[1]);
    for (int y = 0; y < out->extent[1]; y++) {
        float *dst = (float *)out->host + y * out->stride[1];
        for (int x = 0; x < out->extent[0]; x++) {
            int x_coord = x + out->min[0];
            int y_coord = y + out->min[1];
            dst[x] = sinf(x_coord + y_coord);
        }
    }
    return 0;
}

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" DLLEXPORT int make_data_multi(buffer_t *out1, buffer_t *out2) {
    if (!out1->host || !out2->host) {
        // Bounds query mode. We're ok with any requested output size (Halide guarantees they match).
        return 0;
    }
    assert(out1->host && out1->elem_size == 4 && out1->stride[0] == 1);
    assert(out2->host && out2->elem_size == 4 && out2->stride[0] == 1);
    assert(out1->min[0] == out2->min[0] &&
           out1->min[1] == out2->min[1] &&
           out1->extent[0] == out2->extent[0] &&
           out1->extent[1] == out2->extent[1]);
    printf("Generating data over [%d %d] x [%d %d]\n",
           out1->min[0], out1->min[0] + out1->extent[0],
           out1->min[1], out1->min[1] + out1->extent[1]);
    for (int y = 0; y < out1->extent[1]; y++) {
        float *dst1 = (float *)out1->host + y * out1->stride[1];
        float *dst2 = (float *)out2->host + y * out2->stride[1];
        for (int x = 0; x < out1->extent[0]; x++) {
            int x_coord = x + out1->min[0];
            int y_coord = y + out1->min[1];
            dst1[x] = sinf(x_coord + y_coord);
            dst2[x] = cosf(x_coord + y_coord);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func source;
    source.define_extern("make_data",
                         std::vector<ExternFuncArgument>(),
                         Float(32), 2);
    Func sink;
    Var x, y;
    sink(x, y) = source(x, y) - sin(x + y);

    Var xi, yi;
    sink.tile(x, y, xi, yi, 32, 32);

    // Compute the source per tile of sink
    source.compute_at(sink, x);

    Image<float> output = sink.realize(100, 100);

    // Should be all zeroes.
    RDom r(output);
    float error = evaluate_may_gpu<float>(sum(abs(output(r.x, r.y))));
    if (error != 0) {
        printf("Something went wrong\n");
        return -1;
    }

    Func multi;
    std::vector<Type> types;
    types.push_back(Float(32));
    types.push_back(Float(32));
    multi.define_extern("make_data_multi",
                        std::vector<ExternFuncArgument>(),
                        types, 2);
    Func sink_multi;
    sink_multi(x, y) = multi(x, y)[0] - sin(x + y) +
                       multi(x, y)[1] - cos(x + y);

    sink_multi.tile(x, y, xi, yi, 32, 32);

    // Compute the source per tile of sink
    multi.compute_at(sink_multi, x);

    Image<float> output_multi = sink_multi.realize(100, 100);

    // Should be all zeroes.
    float error_multi = evaluate<float>(sum(abs(output_multi(r.x, r.y))));
    if (error_multi != 0) {
        printf("Something went wrong in multi case\n");
        return -1;
    }

    printf("Success!\n");
    return 0;

}
