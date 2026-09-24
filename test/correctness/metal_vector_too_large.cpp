#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("metal_vector_too_large", "Vectorization by widths greater than 4 is not supported by Metal", []() {
        ImageParam input(UInt(16), 2, "input");
        Func f("f");
        Var x("x"), y("y");

        f(x, y) = input(x, y) + 42;
        f.vectorize(x, 16).gpu_blocks(y, DeviceAPI::Metal);

        std::string test_object = Internal::get_test_tmp_dir() + "metal_vector_too_large.o";
        Target mac_target("x86-64-osx-metal");

        f.compile_to_object(test_object, {input}, "f", mac_target);
    });
}
