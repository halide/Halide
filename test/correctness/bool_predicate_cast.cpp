#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoolPredicateCast, Basic) {
    // Test explicit casting of a predicate to an integer as part of a reduction
    // NOTE: triggers a convert_to_bool in Vulkan for a SelectOp
    Target target = get_jit_target_from_environment();
    Var x("x"), y("y");

    Func input("input");
    input(x, y) = cast<uint8_t>(x + y);

    Func test("test");
    test(x, y) = cast(UInt(8), input(x, y) >= 32);

    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        test.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Realization result = test.realize({96, 96});
    Buffer<uint8_t> a = result[0];
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            uint8_t correct_a = ((x + y) >= 32) ? 1 : 0;
            ASSERT_EQ(a(x, y), correct_a);
        }
    }
}
