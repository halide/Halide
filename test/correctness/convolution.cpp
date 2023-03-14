#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // int W = 64*3, H = 64*3;
    const int W = 128, H = 48;

    Buffer<uint16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }

    Var x("x"), y("y");

    Buffer<uint16_t> tent(3, 3);
    tent(0, 0) = 1;
    tent(0, 1) = 2;
    tent(0, 2) = 1;
    tent(1, 0) = 2;
    tent(1, 1) = 4;
    tent(1, 2) = 2;
    tent(2, 0) = 1;
    tent(2, 1) = 2;
    tent(2, 2) = 1;

    Func input("input");
    input(x, y) = in(clamp(x, 0, W - 1), clamp(y, 0, H - 1));
    input.compute_root();

    RDom r(tent);

    /* This first zeros blur1, and then accumulates into it. I.e. the for loops look like:
     * for y:
     *   for x:
     *     blur1(x, y) = 0
     * for y:
     *   for x:
     *     for r.y:
     *       for r.x:
     *         blur1(x, y) += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1)
     *
     * In general, reductions iterate over the reduction domain outermost.
     */
    Func blur1("blur1");
    blur1(x, y) += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1);

    /* This uses an inline reduction, and is the more traditional way
     * of scheduling a convolution. "sum" creates an anonymous
     * reduction function that is computed within the for loop over x
     * in blur2. blur2 isn't actually a reduction. The loop nest looks like:
     * for y:
     *   for x:
     *     tmp = 0
     *     for r.y:
     *       for r.x:
     *         tmp += tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1)
     *     blur(x, y) = tmp
     */
    Func blur2("blur2");
    blur2(x, y) = sum(tent(r.x, r.y) * input(x + r.x - 1, y + r.y - 1));

    Target target = get_jit_target_from_environment();

    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");

        // Initialization (basically memset) done in a GPU kernel
        blur1.gpu_tile(x, y, xi, yi, 16, 16);

        // Summation is done as an outermost loop on the cpu
        blur1.update().reorder(x, y, r.x, r.y).gpu_tile(x, y, xi, yi, 16, 16);

        // Summation is done as a sequential loop within each gpu thread
        blur2.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        int hvx_vector_width = 64;
        // Take this opportunity to test scheduling the pure dimensions in a reduction
        Var xi("xi"), yi("yi");
        blur1.hexagon().tile(x, y, xi, yi, 6, 6);
        // TODO: Add parallel to the schedule.
        blur1.update().hexagon().tile(x, y, xi, yi, hvx_vector_width, 4).vectorize(xi);

        // TODO: Add parallel to the schedule.
        blur2.hexagon().vectorize(x, hvx_vector_width);
    } else {
        // Take this opportunity to test scheduling the pure dimensions in a reduction
        Var xi("xi"), yi("yi");
        blur1.tile(x, y, xi, yi, 6, 6);
        blur1.update().tile(x, y, xi, yi, 4, 4).vectorize(xi).parallel(y);

        blur2.vectorize(x, 4).parallel(y);
    }

    Buffer<uint16_t> out1 = blur1.realize({W, H}, target);
    Buffer<uint16_t> out2 = blur2.realize({W, H}, target);

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            uint16_t correct = (1 * in(x - 1, y - 1) + 2 * in(x, y - 1) + 1 * in(x + 1, y - 1) +
                                2 * in(x - 1, y) + 4 * in(x, y) + 2 * in(x + 1, y) +
                                1 * in(x - 1, y + 1) + 2 * in(x, y + 1) + 1 * in(x + 1, y + 1));
            if (out1(x, y) != correct) {
                printf("out1(%d, %d) = %d instead of %d\n", x, y, out1(x, y), correct);
                return 1;
            }
            if (out2(x, y) != correct) {
                printf("out2(%d, %d) = %d instead of %d\n", x, y, out2(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
