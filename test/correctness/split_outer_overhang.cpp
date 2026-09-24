#include "Halide.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Halide;

void check_1d(Buffer<int> result, const std::string &case_name, int extent) {
    for (int x = 0; x < extent; x++) {
        if (result(x) != 1) {
            std::cerr << case_name << " failed at x=" << x
                      << ": expected 1, got " << result(x) << "\n";
            abort();
        }
    }
}

void check_2d(Buffer<int> result, const std::string &case_name, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (result(x, y) != 1) {
                std::cerr << case_name << " failed at x=" << x << ", y=" << y
                          << ": expected 1, got " << result(x, y) << "\n";
                abort();
            }
        }
    }
}

// A single ShiftInwardsAndBlend split must update every point exactly once,
// whether or not the extent is a multiple of the factor, and also when it is
// smaller than the factor.
void test_shift_inwards_and_blend(int extent, int factor, bool vectorized) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("shift_inwards_and_blend"), out("out");

    f(x) = 0;
    f(x) = f(x) + 1;
    out(x) = f(x);
    f.compute_root();

    f.update().split(x, xo, xi, factor, TailStrategy::ShiftInwardsAndBlend);
    if (vectorized) {
        f.update().vectorize(xi);
    }

    check_1d(out.realize({extent}), "shift_inwards_and_blend", extent);
}

void test_split_outer_overhang(TailStrategy child_tail, int extent, int parent_factor, int child_factor) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("split_outer_overhang");

    f(x) = 0;
    f(x) = f(x) + 1;

    f.update()
        .split(x, xo, xi, parent_factor, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, child_factor, child_tail);

    check_1d(f.realize({extent}), "split_outer_overhang", extent);
}

void test_fused_outer_overhang(TailStrategy child_tail, int width, int height, int parent_factor, int child_factor) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), t("t"), to("to"), ti("ti");
    Func f("fused_outer_overhang");

    f(x, y) = 0;
    f(x, y) = f(x, y) + 1;

    f.update()
        .split(x, xo, xi, parent_factor, TailStrategy::ShiftInwardsAndBlend)
        .fuse(y, xo, t)
        .split(t, to, ti, child_factor, child_tail);

    check_2d(f.realize({width, height}), "fused_outer_overhang", width, height);
}

int main(int argc, char **argv) {
    for (int factor : {3, 8}) {
        for (int extent = 1; extent <= 20; extent++) {
            test_shift_inwards_and_blend(extent, factor, false);
            test_shift_inwards_and_blend(extent, factor, true);
        }
    }

    for (TailStrategy child_tail : {TailStrategy::RoundUp, TailStrategy::Auto}) {
        for (int extent : {8, 15, 16, 19, 20, 24, 25, 32}) {
            test_split_outer_overhang(child_tail, extent, 8, 2);
            test_split_outer_overhang(child_tail, extent, 8, 3);
        }

        for (int width : {16, 19, 20, 24}) {
            for (int height : {2, 3, 4}) {
                test_fused_outer_overhang(child_tail, width, height, 8, 5);
                test_fused_outer_overhang(child_tail, width, height, 8, 7);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
