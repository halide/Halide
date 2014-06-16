#include <Halide.h>
#include <iostream>

#include "../common/gpu_object_lifetime.h"

using namespace Halide;

void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    record_gpu_debug(str);
}

int main(int argc, char *argv[]) {
    for (int i = 0; i < 2; i++) {
        Var x;

        Func kernel1;
        kernel1(x) = floor((x + 0.5f) / 3.0f);

        Func kernel2;
        kernel2(x) = sqrt(4 * x * x) + kernel1(x);

        Func kernel3;
        kernel3(x) = cast<int32_t>(x + kernel2(x));

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            kernel1.gpu_tile(x, 32, GPU_Default).compute_root();
            kernel2.gpu_tile(x, 32, GPU_Default).compute_root();
            kernel3.gpu_tile(x, 32, GPU_Default);
        }

        kernel3.set_custom_print(halide_print);
        // We need gpu_debug to record object creation.
        target.features |= Target::GPUDebug;

        Image<int32_t> result = kernel3.realize(256, target);

        for (int i = 0; i < 256; i++) {
            int32_t correct = static_cast<int32_t>(floor(((float)i + 0.5f) / 3.0f) + sqrtf(4.0f * i * i) + i);
            if (result(i) != correct) {
                std::cout << "Error! " << result(i) << " != " << correct << " at " << i << std::endl;
                return -1;
            }
        }
    }

    int ret = validate_gpu_object_lifetime(true /* allow_globals */);
    if (ret != 0) {
        return ret;
    }

    std::cout << "Success!" << std::endl;
    return 0;
}
