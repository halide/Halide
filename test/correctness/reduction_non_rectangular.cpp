#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (0) {
        Func f;
        Var x, y;
        f(x, y) = x + y;

        RDom r(0, 14, 0, 11);
        r.bound(r.x < r.y);
        f(r.x, r.y) += 1;

        Image<int> im = f.realize(20, 20);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 13) && (0 <= y && y <= 10)) {
                    correct += (x < y) ? 1 : 0;
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    if (0) {
        Func f;
        Var x, y;
        f(x, y) = x + y;

        RDom r(0, 14, 0, 11);
        r.bound(r.x < r.y);
        f(r.x, r.y) += 1;

        RVar rx_outer, rx_inner, r_fused;
        f.update().reorder(r.y, r.x);
        f.update().split(r.x, rx_outer, rx_inner, 4);
        f.update().fuse(rx_inner, r.y, r_fused);

        Image<int> im = f.realize(20, 20);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 13) && (0 <= y && y <= 10)) {
                    correct += (x < y) ? 1 : 0;
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }


    if (0) {
        Func f, g;
        Var x, y;
        f(x, y) = x + y;

        RDom r(0, 100, 0, 100);
        r.bound(2*r.x + 30 < r.y);
        r.bound(r.y >= 100 - r.x);
        f(r.x, r.y) += 1;

        g(x, y) = 2*f(x, y);

        f.compute_root();

        Image<int> im = g.realize(20, 20);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    correct += ((2*x + 30 < y) && (y >= 100 - x)) ? 1 : 0;
                }
                correct *= 2;
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    if (0) {
        Func f;
        Var x, y, z;
        f(x, y, z) = x + y + z;

        RDom r(0, 200, 0, 100);
        r.bound(r.x < r.y + z);
        f(r.x, r.y, z) += 1;

        Image<int> im = f.realize(200, 100, 50);
        for (int z = 0; z < im.channels(); z++) {
            for (int y = 0; y < im.height(); y++) {
                for (int x = 0; x < im.width(); x++) {
                    int correct = x + y + z;
                    if ((0 <= x && x <= 199) && (0 <= y && y <= 99)) {
                        correct += (x < y + z) ? 1 : 0;
                    }
                    if (im(x, y, z) != correct) {
                        printf("im(%d, %d, %d) = %d instead of %d\n",
                               x, y, z, im(x, y, z), correct);
                        return -1;
                    }
                }
            }
        }
    }

    if (0) {
        Func f, g;
        Var x, y;

        g(x) = x;

        f(x, y) = x + y;

        RDom r(0, 100, 0, 50);
        r.bound(r.x < g(r.y));
        f(r.x, r.y) += 1;

        g.compute_root();

        Image<int> im = f.realize(200, 200);

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 49)) {
                    correct += (x < y) ? 1 : 0;
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    if (1) {
        Func f, g;
        Var x, y;

        g(x, y) = x;
        f(x, y) = x + y;

        RDom r(0, 100, 0, 100);
        r.bound(r.x < r.y);
        f(r.x, r.y) = g(r.x, r.y);

        g.compute_at(f, r.y);

        Image<int> im = f.realize(200, 200);

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    if (x < y) {
                        correct = x;
                    }
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;

}
