#include <Halide.h>
#include <stdio.h>

using namespace Halide;

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" int make_data(buffer_t *out) {
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
    float error = evaluate<float>(sum(abs(output(r.x, r.y))));
    if (error != 0) {
        printf("Something went wrong\n");
        return -1;
    }

    printf("Success!\n");
    return 0;

}
