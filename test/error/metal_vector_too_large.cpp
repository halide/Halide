#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestMetalVectorTooLarge() {
    ImageParam input(UInt(16), 2, "input");
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = input(x, y) + 42;
    f.vectorize(x, 16).gpu_blocks(y, DeviceAPI::Metal);

    std::string test_object = Internal::get_test_tmp_dir() + "metal_vector_too_large.o";
    Target mac_target("x86-64-osx-metal");

    f.compile_to_object(test_object, {input}, "f", mac_target);
}
}  // namespace

TEST(ErrorTests, MetalVectorTooLarge) {
    EXPECT_COMPILE_ERROR(
        TestMetalVectorTooLarge,
        HasSubstr("Vectorization by widths greater than 4 is not supported by Metal "
                  "-- type is uint16x16."));
}
