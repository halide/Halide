#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");

    // Create a simple pipeline that scales pixel values by 2.
    ImageParam input(Int(32), 2);

    Func f("f");
    f(x, y) = input(x, y) * 2;

    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, 16, 16);
    }

    f.compile_to_file("gpu_only", input);

    return 0;
}
