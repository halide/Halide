#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

template<typename T>
bool check_bool_vector_cast(const Target &target, int lanes) {
    if (!target.supports_type(type_of<T>(), DeviceAPI::Vulkan)) {
        return true;
    }

    ImageParam input(Int(32), 1, "input");
    Var x("x"), xo("xo"), xi("xi");
    Func output("output");
    Expr cond = cast<bool>(input(x)) && (input(x) > 0);
    output(x) = cast<T>(cond);
    output.gpu_tile(x, xo, xi, 32).vectorize(xi, lanes);

    Buffer<int32_t> in(64);
    for (int i = 0; i < 64; i++) {
        in(i) = i - 32;
    }
    input.set(in);
    Buffer<T> out = output.realize({64}, target);

    for (int i = 0; i < 64; i++) {
        float expected = (in(i) != 0 && in(i) > 0) ? 1 : 0;
        if ((float)out(i) != expected) {
            std::cout << type_of<T>() << " x " << lanes << ": out(" << i << ") = "
                      << (float)out(i) << " instead of " << expected << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::Vulkan)) {
        printf("[SKIP] Vulkan not enabled.\n");
        return 0;
    }

    for (int lanes : {2, 4, 8}) {
        if (!check_bool_vector_cast<int8_t>(target, lanes) ||
            !check_bool_vector_cast<uint8_t>(target, lanes) ||
            !check_bool_vector_cast<int16_t>(target, lanes) ||
            !check_bool_vector_cast<uint16_t>(target, lanes) ||
            !check_bool_vector_cast<int32_t>(target, lanes) ||
            !check_bool_vector_cast<uint32_t>(target, lanes) ||
            !check_bool_vector_cast<int64_t>(target, lanes) ||
            !check_bool_vector_cast<uint64_t>(target, lanes) ||
            !check_bool_vector_cast<float16_t>(target, lanes) ||
            !check_bool_vector_cast<bfloat16_t>(target, lanes) ||
            !check_bool_vector_cast<float>(target, lanes) ||
            !check_bool_vector_cast<double>(target, lanes)) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
