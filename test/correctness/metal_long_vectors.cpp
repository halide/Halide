#include "Halide.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(UInt(16), 2, "input");
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = input(x, y) + 42;
    f.vectorize(x, 32).gpu_blocks(y, DeviceAPI::Metal);

    std::string test_object = Internal::get_test_tmp_dir() + "metal_vector_too_large.o";
    Target mac_target("x86-64-osx-metal");

    f.compile_to_object(test_object, {input}, "f", mac_target);

    printf("Success!\n");
    return 0;
}
