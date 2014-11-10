#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char *argv[]) {
    Var x;

    Func kernel1;
    kernel1(x) = floor((x + 0.5f) / 3.0f);

    Func kernel2;
    kernel2(x) = sqrt(4 * x * x) + kernel1(x);

    Func kernel3;
    kernel3(x) = cast<int32_t>(x + kernel2(x));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        kernel1.gpu_tile(x, 32, Device_Default_GPU).compute_root();
        kernel2.gpu_tile(x, 32, Device_Default_GPU).compute_root();
        kernel3.gpu_tile(x, 32, Device_Default_GPU);
    }

    Image<int32_t> result = kernel3.realize(256, target);

    for (int i = 0; i < 256; i++) {
        assert(result(i) == static_cast<int32_t>(floor(((float)i + 0.5f) / 3.0f) + sqrtf(4.0f * i * i) + i));
    }

    std::cout << "Success!" << std::endl;
}
