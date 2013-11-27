#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char *argv[]) {
    Var x;

    Func kernel1;
    kernel1(x) = floor(x / 3.0f);

    Func kernel2;
    kernel2(x) = sqrt(4 * x * x) + kernel1(x);

    Func kernel3;
    kernel3(x) = cast<int32_t>(x + kernel2(x));

    Target target = get_target_from_environment();
    if (target.features & Target::CUDA ||
	target.features & Target::OpenCL) {
        kernel1.cuda_tile(x, 32).compute_root();
        kernel2.cuda_tile(x, 32).compute_root();

        kernel3.cuda_tile(x, 32);
    }

    Image<int32_t> result = kernel3.realize(256);

    for (int i = 0; i < 256; i ++)
      assert(result(i) == static_cast<int32_t>(floor(i / 3.0f) + sqrt(4 * i * i) + i));

    std::cout << "Success!" << std::endl;
}
