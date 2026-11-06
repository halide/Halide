#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

template<typename T>
void check_bool_vector_cast(const Target &target, int lanes) {
    ImageParam input(Int(32), 1, "input");
    Var x("x"), xo("xo"), xi("xi");
    Func output("output");
    Expr cond = cast<bool>(input(x)) && (input(x) > 0);
    output(x) = cast<T>(cond);
    output.gpu_tile(x, xo, xi, 32).vectorize(xi, lanes);
    output.compile_to_module({input}, "vulkan_bool_vector_cast", target);
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_host_target();
    target.set_features({
        Target::Vulkan,
        Target::VulkanInt8,
        Target::VulkanInt16,
        Target::VulkanInt64,
        Target::VulkanFloat16,
        Target::VulkanFloat64,
    });

    for (int lanes : {2, 4, 8}) {
        check_bool_vector_cast<int8_t>(target, lanes);
        check_bool_vector_cast<uint8_t>(target, lanes);
        check_bool_vector_cast<int16_t>(target, lanes);
        check_bool_vector_cast<uint16_t>(target, lanes);
        check_bool_vector_cast<int32_t>(target, lanes);
        check_bool_vector_cast<uint32_t>(target, lanes);
        check_bool_vector_cast<int64_t>(target, lanes);
        check_bool_vector_cast<uint64_t>(target, lanes);
        check_bool_vector_cast<float16_t>(target, lanes);
        check_bool_vector_cast<bfloat16_t>(target, lanes);
        check_bool_vector_cast<float>(target, lanes);
        check_bool_vector_cast<double>(target, lanes);
    }

    printf("Success!\n");
    return 0;
}
