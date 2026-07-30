#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A Func whose extent depends on a runtime Param (forcing dynamic, rather
// than fixed-size, allocation) is chained through two reduction stages with
// no explicit schedule, inside a gpu_tile whose tail uses GuardWithIf. This
// produces several mutually-exclusive boundary-condition branches, each of
// which must correctly allocate and free the intermediate buffer.
int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::Metal)) {
        printf("[SKIP] Metal not enabled\n");
        return 0;
    }

    Var x("x"), y("y");

    Func input("input");
    input(x, y) = cast<int16_t>((x * 7 + y * 13) % 100);

    Param<int> radius("radius");
    radius.set(3);

    Func h_blur("h_blur"), v_blur("v_blur"), result("result");

    RDom rh(1, radius, "rh");
    h_blur(x, y) = cast<int32_t>(input(x, y));
    h_blur(x, y) += cast<int32_t>(input(x - rh.x, y)) + cast<int32_t>(input(x + rh.x, y));

    RDom rv(1, radius, "rv");
    v_blur(x, y) = h_blur(x, y);
    v_blur(x, y) += h_blur(x, y - rv.x) + h_blur(x, y + rv.x);

    result(x, y) = cast<int16_t>(v_blur(x, y) >> 4);

    Var xi, yi;
    result.gpu_tile(x, y, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    constexpr int radius_val = 3, w = 33, h = 33;
    Buffer<int16_t> actual = result.realize({w, h}, t);
    actual.copy_to_host();

    auto in = [](int x, int y) -> int32_t {
        // Halide's % is Euclidean (always non-negative for a positive
        // divisor); replicate that here since x, y can be negative.
        int32_t v = (x * 7 + y * 13) % 100;
        if (v < 0) v += 100;
        return (int16_t)v;
    };
    auto h_blur_ref = [&](int x, int y) -> int32_t {
        int32_t v = in(x, y);
        for (int i = 1; i <= radius_val; i++) {
            v += in(x - i, y) + in(x + i, y);
        }
        return v;
    };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int32_t v_blur_ref = h_blur_ref(x, y);
            for (int i = 1; i <= radius_val; i++) {
                v_blur_ref += h_blur_ref(x, y - i) + h_blur_ref(x, y + i);
            }
            int16_t expected = (int16_t)(v_blur_ref >> 4);
            if (actual(x, y) != expected) {
                printf("Mismatch at (%d, %d): expected %d, got %d\n", x, y, expected, actual(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
