#include "Halide.h"
#include <stdio.h>
#include <algorithm>

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// Use an extern stage to do a sort
extern "C" DLLEXPORT int sort_buffer(buffer_t *in, buffer_t *out) {
    if (!in->host) {
        in->min[0] = out->min[0];
        in->extent[0] = out->extent[0];
    } else {
        memcpy(out->host, in->host, out->extent[0] * out->elem_size);
        float *out_start = (float *)out->host;
        float *out_end = out_start + out->extent[0];
        std::sort(out_start, out_end);
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
    Image<float> output = sorted.realize(100);

    // Check the output
    Image<float> reference = lambda(x, sin(x)).realize(100);
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
