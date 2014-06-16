#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");

    // The input tile.
    ImageParam input(Int(32), 2);

    Func f("f");
    f(x, y) = 2*input(x, y);

    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 16, 16);
    }
    // The test requires gpu_debug to examine the output.
    target.features |= Target::GPUDebug;

    f.compile_to_file("func_gpu_object_lifetime", input, target);

    return 0;
}
