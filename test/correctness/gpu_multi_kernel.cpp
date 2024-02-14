#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char *argv[]) {
    Var x, xi;

    Func kernel1;
    kernel1(x) = floor((x + 0.5f) / 3.0f);

    Func kernel2;
    kernel2(x) = sqrt(4 * x * x) + kernel1(x);

    Func kernel3;
    kernel3(x) = cast<int32_t>(round(x + kernel2(x)));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        kernel1.gpu_tile(x, xi, 32).compute_root();
        kernel2.gpu_tile(x, xi, 32).compute_root();
        kernel3.gpu_tile(x, xi, 32);
    } else {
        kernel1.compute_root();
        kernel2.compute_root();
    }

    Buffer<int32_t> result = kernel3.realize({256}, target);

    for (int i = 0; i < 256; i++) {
        float a = floor((i + 0.5f) / 3.0f);
        float b = sqrt(4 * i * i) + a;
        int c = (int32_t)(round(i + b));
        assert(result(i) == c);
    }

    printf("Success!\n");
    return 0;
}
