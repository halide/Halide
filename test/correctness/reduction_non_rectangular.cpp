#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    //TODO: the current algorithm can't seem to simplify this one
    if (0) {
        Func f("f");
        Var x("x"), y("y");
        f(x, y) = x + y;

        RDom r(0, 20, 0, 20);
        r.where(r.x < r.y);
        r.where(r.x == 10);
        f(r.x, r.y) += 1;

        Image<int> im = f.realize(50, 50);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((x == 10) && (0 <= y && y <= 19)) {
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
        Func f("f");
        Var x("x"), y("y");
        f(x, y) = x + y;

        RDom r(0, 14, 0, 11);
        r.where(r.x < r.y);
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
        Func f("f"), g("g");
        Var x("x"), y("y");
        f(x, y) = x + y;

        RDom r(0, 100, 0, 100);
        r.where(2*r.x + 30 < r.y);
        r.where(r.y >= 100 - r.x);
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
        Func f("f");
        Var x("x"), y("y"), z("z");
        f(x, y, z) = x + y + z;

        RDom r(0, 200, 0, 100, "r");
        r.where(r.x < r.y + z);
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
        Func f("f"), g("g");
        Var x("x"), y("y");

        g(x) = x;

        f(x, y) = x + y;

        RDom r(0, 100, 0, 50, "r");
        r.where(r.x < g(r.y));
        std::cout << "*******Defining RDom: \n" << r << "\n";
        f(r.x, r.y) += 1;

        std::cout << "*******schedule g to compute at root\n";
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

    if (0) {
        Func f("f"), g("g");
        Var x("x"), y("y");

        g(x, y) = x;
        f(x, y) = x + y;

        RDom r(0, 100, 0, 100);
        r.where(r.x < r.y);
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

    if (1) {
        Func f("f");
        Var x("x"), y("y");
        f(x, y) = x + y;

        RDom r(10, 20, 10, 20);
        r.where((r.x + r.y) % 2 == 0);
        f(r.x, r.y) = f(r.x-1, r.y) + 3;

        Image<int> im = f.realize(50, 50);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((10 <= x && x <= 29) && (10 <= y && y <= 29)) {
                    correct += ((x + y) % 2 == 0) ? 2 : 0;
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
