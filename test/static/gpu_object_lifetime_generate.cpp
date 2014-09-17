#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {

    Var x;

    Func f;
    f(x) = x;

    Target target = get_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, 16);
    }
    // The test requires gpu_debug to examine the output.
    target.set_feature(Target::Debug);

    f.compile_to_file("func_gpu_object_lifetime", target);

    return 0;
}
