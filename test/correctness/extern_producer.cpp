#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Some helper functions for rounding
int round_down(int, int);
int round_up(int x, int m) {
    if (x < 0) {
        return -round_down(-x, m);
    } else {
        return ((x + m - 1) / m) * m;
    }
}

int round_down(int x, int m) {
    if (x < 0) {
        return -round_up(-x, m);
    } else {
        return (x / m) * m;
    }
}

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using a
// periodic integer function.
extern "C" HALIDE_EXPORT_SYMBOL int make_data(halide_buffer_t *out) {
    static int desired_row_extent = 0;
    if (out->is_bounds_query()) {
        // Bounds query mode. To make life interesting, let's add some
        // arbitrary constraints on what we can produce.

        // The start and end of the x coord must be a multiple of 10.
        int max_plus_one = out->dim[0].min + out->dim[0].extent;
        max_plus_one = round_up(max_plus_one, 10);
        out->dim[0].min = round_down(out->dim[0].min, 10);
        out->dim[0].extent = max_plus_one - out->dim[0].min;
        desired_row_extent = out->dim[0].extent;

        // There must be at least 40 scanlines.
        if (out->dim[1].extent < 40) {
            out->dim[1].extent = 40;
        }
        return 0;
    }
    assert(out->host);
    assert(out->type == halide_type_of<int>());
    assert(out->dimensions == 2);
    assert(out->dim[0].stride == 1);
    // Check that the row stride is 128B/32-element aligned.
    assert(out->dim[1].stride == (out->dim[1].stride) / 32 * 32);
    // Check that the row extent is not changed due to alignment.
    assert(out->dim[0].extent == desired_row_extent);
    printf("Generating data over [%d %d] x [%d %d]\n",
           out->dim[0].min, out->dim[0].min + out->dim[0].extent,
           out->dim[1].min, out->dim[1].min + out->dim[1].extent);
    for (int y = 0; y < out->dim[1].extent; y++) {
        int *dst = (int *)out->host + y * out->dim[1].stride;
        for (int x = 0; x < out->dim[0].extent; x++) {
            int x_coord = x + out->dim[0].min;
            int y_coord = y + out->dim[1].min;
            dst[x] = (x_coord + y_coord) % 61;
        }
    }
    return 0;
}

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using a
// periodic integer function.
extern "C" HALIDE_EXPORT_SYMBOL int make_data_multi(halide_buffer_t *out1, halide_buffer_t *out2) {
    if (!out1->host || !out2->host) {
        // Bounds query mode. We're ok with any requested output size (Halide guarantees they match).
        return 0;
    }
    assert(out1->dimensions == 2 && out2->dimensions == 2);
    assert(out1->host && out1->type == halide_type_of<int>() && out1->dim[0].stride == 1);
    assert(out2->host && out2->type == halide_type_of<int>() && out2->dim[0].stride == 1);
    assert(out1->dim[0].min == out2->dim[0].min &&
           out1->dim[1].min == out2->dim[1].min &&
           out1->dim[0].extent == out2->dim[0].extent &&
           out1->dim[1].extent == out2->dim[1].extent);
    printf("Generating data over [%d %d] x [%d %d]\n",
           out1->dim[0].min, out1->dim[0].min + out1->dim[0].extent,
           out1->dim[1].min, out1->dim[1].min + out1->dim[1].extent);
    for (int y = 0; y < out1->dim[1].extent; y++) {
        int *dst1 = (int *)out1->host + y * out1->dim[1].stride;
        int *dst2 = (int *)out2->host + y * out2->dim[1].stride;
        for (int x = 0; x < out1->dim[0].extent; x++) {
            int x_coord = x + out1->dim[0].min;
            int y_coord = y + out1->dim[1].min;
            dst1[x] = (x_coord + y_coord) % 61;
            dst2[x] = (x_coord + y_coord + 15) % 61;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Var x, y;
    Var xi, yi;
    {
        Func source;
        source.define_extern("make_data",
                             std::vector<ExternFuncArgument>(),
                             Int(32), {x, y});
        // Row stride should be 128B/32-element aligned.
        source.align_storage(x, 32);
        Func sink;
        sink(x, y) = source(x, y) - (x + y) % 61;

        sink.tile(x, y, xi, yi, 32, 32);

        // Compute the source per tile of sink
        source.compute_at(sink, x);

        Buffer<int> output = sink.realize({100, 100});

        // Should be all zeroes.
        RDom r(output);
        unsigned int error = evaluate_may_gpu<unsigned int>(sum(abs(output(r.x, r.y))));
        if (error != 0) {
            printf("Something went wrong\n");
            return 1;
        }
    }

    {
        Func multi;
        std::vector<Type> types;
        types.push_back(Int(32));
        types.push_back(Int(32));
        multi.define_extern("make_data_multi",
                            std::vector<ExternFuncArgument>(),
                            types, {x, y});
        Func sink_multi;
        sink_multi(x, y) = multi(x, y)[0] - (x + y) % 61 +
                           multi(x, y)[1] - (x + y + 15) % 61;

        sink_multi.tile(x, y, xi, yi, 32, 32);

        // Compute the source per tile of sink
        multi.compute_at(sink_multi, x);

        Buffer<int> output_multi = sink_multi.realize({100, 100});

        // Should be all zeroes.
        RDom r(output_multi);
        unsigned int error_multi = evaluate<unsigned int>(sum(abs(output_multi(r.x, r.y))));
        if (error_multi != 0) {
            printf("Something went wrong in multi case\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
