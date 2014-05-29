#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");
    ImageParam input(Float(32), 2);
    Func f("f");

    f(x, y) = input(x, y) * 2.0f + 1.0f;

    // Use the GPU for this f if a GPU is available.
    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 16, 16).compute_root();
    }

    f.compile_to_file("acquire_release", input);

    return 0;
}
