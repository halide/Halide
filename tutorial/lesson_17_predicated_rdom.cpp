// Halide tutorial lesson 17: Reductions over non-rectangular domains

// This lesson demonstrates how to define updates that iterate over
// subsets of a reduction domain using predicates.

// On linux, you can compile and run it like so:
// g++ lesson_17*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_17 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_17

// On os x:
// g++ lesson_17*.cpp -g -I ../include -L ../bin -lHalide -o lesson_17 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_17

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_17_predicated_rdom
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // In lesson 9, we learned how to use RDom to define a "reduction
    // domain" to use in a Halide update definition. The domain
    // defined by an RDom, however, is always rectangular, and the
    // update occurs at every point in that rectangular domain. In
    // some cases, we might want to iterate over some non-rectangular
    // domain, e.g. a circle. We can achieve this behavior by using
    // the RDom::where directive.

    {
        // Starting with this pure definition:
        Func circle("circle");
        Var x("x"), y("y");
        circle(x, y) = x + y;

        // Say we want an update that squares the values inside a
        // circular region centered at (3, 3) with radius of 3. To do
        // this, we first define the minimal bounding box over the
        // circular region using an RDom.
        RDom r(0, 7, 0, 7);

        // The bounding box does not have to be minimal. In fact, the
        // box can be of any size, as long it covers the region we'd
        // like to update. However, the tighter the bounding box, the
        // tighter the generated loop bounds will be. Halide will
        // tighten the loop bounds automatically when possible, but in
        // general, it is better to define a minimal bounding box.

        // Then, we use RDom::where to define the predicate over that
        // bounding box, such that the update is performed only if the
        // given predicate evaluates to true, i.e. within the circular
        // region.
        r.where((r.x - 3)*(r.x - 3) + (r.y - 3)*(r.y - 3) <= 10);

        // After defining the predicate, we then define the update.
        circle(r.x, r.y) *= 2;

        Buffer<int> halide_result = circle.realize(7, 7);

        // See figures/lesson_17_rdom_circular.mp4 for a visualization of
        // what this did.

        // The equivalent C is:
        int c_result[7][7];
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 7; x++) {
                c_result[y][x] = x + y;
            }
        }
        for (int r_y = 0; r_y < 7; r_y++) {
            for (int r_x = 0; r_x < 7; r_x++) {
                // Update is only performed if the predicate evaluates to true.
                if ((r_x - 3)*(r_x - 3) + (r_y - 3)*(r_y - 3) <= 10) {
                    c_result[r_y][r_x] *= 2;
                }
            }
        }

        // Check the results match:
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 7; x++) {
                if (halide_result(x, y) != c_result[y][x]) {
                    printf("halide_result(%d, %d) = %d instead of %d\n",
                           x, y, halide_result(x, y), c_result[y][x]);
                    return -1;
                }
            }
        }
    }

    {
        // We can also define multiple predicates over an RDom. Let's
        // say now we want the update to happen within some triangular
        // region. To do this we define three predicates, where each
        // corresponds to one side of the triangle.
        Func triangle("triangle");
        Var x("x"), y("y");
        triangle(x, y) = x + y;
        // First, let's define the minimal bounding box over the triangular
        // region.
        RDom r(0, 8, 0, 10);
        // Next, let's add the three predicates to the RDom using
        // multiple calls to RDom::where
        r.where(r.x + r.y > 5);
        r.where(3*r.y - 2*r.x < 15);
        r.where(4*r.x - r.y < 20);

        // We can also pack the multiple predicates into one like so:
        // r.where((r.x + r.y > 5) && (3*r.y - 2*r.x < 15) && (4*r.x - r.y < 20));

        // Then define the update.
        triangle(r.x, r.y) *= 2;

        Buffer<int> halide_result = triangle.realize(10, 10);

        // See figures/lesson_17_rdom_triangular.mp4 for a
        // visualization of what this did.

        // The equivalent C is:
        int c_result[10][10];
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                c_result[y][x] = x + y;
            }
        }
        for (int r_y = 0; r_y < 10; r_y++) {
            for (int r_x = 0; r_x < 8; r_x++) {
                // Update is only performed if the predicate evaluates to true.
                if ((r_x + r_y > 5) && (3*r_y - 2*r_x < 15) && (4*r_x - r_y < 20)) {
                    c_result[r_y][r_x] *= 2;
                }
            }
        }

        // Check the results match:
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                if (halide_result(x, y) != c_result[y][x]) {
                    printf("halide_result(%d, %d) = %d instead of %d\n",
                           x, y, halide_result(x, y), c_result[y][x]);
                    return -1;
                }
            }
        }
    }

    {
        // The predicate is not limited to the RDom's variables only
        // (r.x, r.y, ...).  It can also refer to free variables in
        // the update definition, and even make calls to other Funcs,
        // or make recursive calls to the same Func. For example:
        Func f("f"), g("g");
        Var x("x"), y("y");
        f(x, y) = 2 * x + y;
        g(x, y) = x + y;

        // This RDom's predicates depend on the initial value of 'f'.
        RDom r1(0, 5, 0, 5);
        r1.where(f(r1.x, r1.y) >= 4);
        r1.where(f(r1.x, r1.y) <= 7);
        f(r1.x, r1.y) /= 10;

        f.compute_root();

        // While this one involves calls to another Func.
        RDom r2(1, 3, 1, 3);
        r2.where(f(r2.x, r2.y) < 1);
        g(r2.x, r2.y) += 17;

        Buffer<int> halide_result_g = g.realize(5, 5);

        // See figures/lesson_17_rdom_calls_in_predicate.mp4 for a
        // visualization of what this did.

        // The equivalent C for 'f' is:
        int c_result_f[5][5];
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 5; x++) {
                c_result_f[y][x] = 2 * x + y;
            }
        }
        for (int r1_y = 0; r1_y < 5; r1_y++) {
            for (int r1_x = 0; r1_x < 5; r1_x++) {
                // Update is only performed if the predicate evaluates to true.
                if ((c_result_f[r1_y][r1_x] >= 4) && (c_result_f[r1_y][r1_x] <= 7)) {
                    c_result_f[r1_y][r1_x] /= 10;
                }
            }
        }

        // And, the equivalent C for 'g' is:
        int c_result_g[5][5];
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 5; x++) {
                c_result_g[y][x] = x + y;
            }
        }
        for (int r2_y = 1; r2_y < 4; r2_y++) {
            for (int r1_x = 1; r1_x < 4; r1_x++) {
                // Update is only performed if the predicate evaluates to true.
                if (c_result_f[r2_y][r1_x] < 1) {
                    c_result_g[r2_y][r1_x] += 17;
                }
            }
        }

        // Check the results match:
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 5; x++) {
                if (halide_result_g(x, y) != c_result_g[y][x]) {
                    printf("halide_result_g(%d, %d) = %d instead of %d\n",
                           x, y, halide_result_g(x, y), c_result_g[y][x]);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
