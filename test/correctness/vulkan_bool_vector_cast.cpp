#include "Halide.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_host_target().with_feature(Target::Vulkan);
    Var x("x"), xo("xo"), xi("xi");
    Func output("output");
    output(x) = cast<int32_t>(x > 0);
    output.gpu_tile(x, xo, xi, 32).vectorize(xi, 8);
    output.compile_to_module({}, "vulkan_bool_vector_cast", target);

    printf("Success!\n");
    return 0;
}
