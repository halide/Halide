#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int check_image(const Image<int> &im, const std::function<int(int,int,int)> &func) {
    for (int z = 0; z < im.channels(); z++) {
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = func(x, y, z);
                if (im(x, y, z) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, im(x, y, z), correct);
                    return -1;
                }
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (0) {
        // Vectorized Load from a vectorized allocation
        const int size = 80;

        Func f("f"), g("g");
        Var x("x"), y("y"), z("z");

        g(x, y, z) = x;

        f(x, y, z) = 100;
        RDom r(0, size, 0, size, 0, size);
        f(r.x, r.y, r.z) += 2*g(r.x*r.z, r.y, r.z);

        f.update(0).vectorize(r.z, 8);

        g.compute_at(f, r.y);
        g.bound_extent(x, size*size);

        Image<int> im = f.realize(size, size, size);

        for (int z = 0; z < im.channels(); z++) {
            for (int y = 0; y < im.height(); y++) {
                for (int x = 0; x < im.width(); x++) {
                    int correct = 100 + 2*x*z;
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
        Func f("f");
        Var x("x"), y("y");

        f(x, y) = x + y;

        RDom r(0, 40, 0, 40);
        r.where(r.x < 24);
        f(r.x, r.y) += r.x*r.y;
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(80, 80);

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                correct += (x < 24) && (y < 40) ? x*y : 0;
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
        Var x, xo, xi;

        f(x) = x;
        g(x) = f(x) + f(x*x-20);

        g.split(x, xo, xi, 4).vectorize(xi);
        f.compute_at(g, xi);

        // The region required of f is [min(x, x*x-20), max(x, x*x-20)],
        // which varies nastily with the var being vectorized.

        Image<int> out = g.realize(100);

        for (int i = 0; i < 4; i++) {
            int correct = i + i*i - 20;
            if (out(i) != correct) {
                printf("out(%d) = %d instead of %d\n",
                       i, out(i), correct);
                return -1;
            }
        }
    }

    if (0) {
        Func f("f"), g("g");
        Var x("x"), y("y"), z("z");

        f(x, y, z) = x + y + z;
        f.compute_root();

        g(x, y, z) = 1;
        RDom r(5, 10, 5, 10, 0, 20);
        r.where(r.x < r.y);
        r.where(r.x + 2*r.y <= r.z);
        g(r.x, r.y, r.z) += f(r.x, r.y, r.z);

        Var u("u"), v("v");
        Func intm = g.update(0).rfactor({{r.y, u}, {r.x, v}});
        intm.compute_root();
        Var ui("ui"), vi("vi"), t("t");
        intm.tile(u, v, ui, vi, 2, 2).fuse(u, v, t).parallel(t);
        intm.update(0).vectorize(r.z, 2);

        Image<int> im = g.realize(20, 20, 20);
        auto func = [](int x, int y, int z) {
            return (5 <= x && x <= 14) && (5 <= y && y <= 14) &&
                   (0 <= z && z <= 19) && (x < y) && (x + 2*y <= z) ? x + y + z + 1 : 1;
        };
        if (check_image(im, func)) {
            return -1;
        }
    }

    if (0) {
        Var x("x");
        Func f ("f"), g("g"), ref("ref");

        g(x) = x;
        g.compute_root();

        RDom r(0, 40, 0, 40);
        r.where(r.x < 24);

        ref(x) = 0;
        ref(r.x) += g(2*r.x) + g(2*r.x + 1);
        Image<int> im_ref = ref.realize(80);

        f(x) = 0;
        f(r.x) += g(2*r.x) + g(2*r.x + 1);
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(80);
        for (int x = 0; x < im.width(); x++) {
            int correct = im_ref(x);
            if (im(x) != correct) {
                printf("im(%d) = %d instead of %d\n",
                       x, im(x), correct);
                return -1;
            }
        }
    }

    if (0) {
        Var x("x"), y("y");
        Func f ("f"), g("g"), ref("ref");

        g(x, y) = x + y;
        g.compute_root();

        RDom r(0, 40, 0, 40);
        r.where(r.x + r.y < 24);

        ref(x, y) = 10;
        // bugs here, need to remove the never executed store/load (conditional false)
        ref(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);
        Image<int> im_ref = ref.realize(80, 80);

        f(x, y) = 10;
        // bugs here, need to remove the never executed store/load (conditional false)
        f(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(80, 80);
        for (int x = 0; x < im.width(); x++) {
            int correct = im_ref(x);
            if (im(x) != correct) {
                printf("im(%d) = %d instead of %d\n",
                       x, im(x), correct);
                return -1;
            }
        }
    }

    if (0) {
        Var x("x");
        Func f ("f"), g("g"), ref("ref");

        g(x) = x;
        g.compute_root();

        RDom r(0, 40, 0, 40);
        r.where(r.x < 30);

        ref(x) = 0;
        ref(2*r.x) += g(2*r.x) + g(2*r.x + 1);
        Image<int> im_ref = ref.realize(80);

        f(x) = 0;
        f(2*r.x) += g(2*r.x) + g(2*r.x + 1);
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(80);
        for (int x = 0; x < im.width(); x++) {
            int correct = im_ref(x);
            if (im(x) != correct) {
                printf("im(%d) = %d instead of %d\n",
                       x, im(x), correct);
                return -1;
            }
        }
    }

    if (0) {
        int size = 50;
        Var x("x"), y("y");
        Func f ("f"), g("g"), ref("ref");

        g(x, y) = x + y;
        g.compute_root();

        RDom r(0, size, 0, size);
        r.where(r.x + r.y < size);

        ref(x, y) = 10;
        ref(r.x, r.y) += g(r.x, r.y) * 2;
        Image<int> im_ref = ref.realize(size, size);

        f(x, y) = 10;
        f(r.x, r.y) += g(r.x, r.y) * 2;
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(size, size);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = im_ref(x, y);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    if (1) {
        int size = 50;
        Var x("x"), y("y");
        Func f ("f"), g("g"), h("h"), ref("ref");

        g(x, y) = x * y;
        g.compute_root();

        h(x, y) = x + y;
        g.compute_root();

        RDom r(0, size, 0, size);
        r.where(r.x*r.x + r.y < 2000);

        ref(x, y) = 10;
        ref(r.x, r.y) += g(r.x, r.y) * 2;
        Image<int> im_ref = ref.realize(size, size);

        f(x, y) = 10;
        f(r.x, r.y) += g(r.x, r.y) * 2;
        f.update(0).vectorize(r.x, 8);

        Image<int> im = f.realize(size, size);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = im_ref(x, y);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success\n");

    return 0;
}
