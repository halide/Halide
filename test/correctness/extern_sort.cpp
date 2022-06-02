#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

// Use an extern stage to do a sort
extern "C" HALIDE_EXPORT_SYMBOL int sort_buffer(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].min = out->dim[0].min;
        in->dim[0].extent = out->dim[0].extent;
    } else {
        memcpy(out->host, in->host, out->dim[0].extent * out->type.bytes());
        float *out_start = (float *)out->host;
        float *out_end = out_start + out->dim[0].extent;
        std::sort(out_start, out_end);
        out->set_host_dirty();
    }
    return 0;
}

int main(int argc, char **argv) {
    Func data;
    Var x;
    data(x) = sin(x);
    data.compute_root();

    Func sorted;
    std::vector<ExternFuncArgument> args;
    args.push_back(data);
    sorted.define_extern("sort_buffer", args, Float(32), 1);
    Buffer<float> output = sorted.realize({100});

    // Check the output
    Buffer<float> reference = lambda(x, sin(x)).realize({100});
    std::sort(&reference(0), &reference(100));

    RDom r(reference);
    float error = evaluate_may_gpu<float>(sum(abs(reference(r) - output(r))));

    if (error != 0) {
        printf("Output incorrect\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
