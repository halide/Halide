#include "Halide.h"
#include "check_call_graphs.h"

using namespace Halide;

int vectorize_2d_round_up() {
    const int width = 32;
    const int height = 24;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize(width, height);

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_2d_guard_with_if() {
    const int width = 33;
    const int height = 22;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize(width, height);

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_2d_inlined_with_update() {
    const int width = 33;
    const int height = 22;

    Func f, inlined;
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom r(0, 10, "r");
    inlined(x) = x;
    inlined(x) += r;
    f(x, y) = inlined(x) + 2 * y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize(width, height);

    // for (int iy = 0; iy < height; iy++) {
    //     for (int ix = 0; ix < width; ix++) {
    //         printf("%2d/%2d ", result(ix, iy), ix + iy + 45);
    //     }
    //     printf("\n");
    // }

    auto cmp_func = [](int x, int y) {
        return x + 2 * y + 45;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_2d_with_inner_for() {
    const int width = 33;
    const int height = 22;

    Func f;
    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
    f(x, y, c) = 3 * x + y + 7 * c;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .reorder(c, xi, yi, x, y)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize(width, height, 3);

    // for (int ic = 0; ic < 3; ic++) {
    //     for (int iy = 0; iy < height; iy++) {
    //         for (int ix = 0; ix < width; ix++) {
    //             printf("%2d ", result(ix, iy, ic));
    //         }
    //         printf("\n");
    //     }
    //     printf("\n");
    // }

    auto cmp_func = [](int x, int y, int c) {
        return 3 * x + y + 7 * c;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_2d_with_compute_at_vectorized() {
    const int width = 16;
    const int height = 16;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi");
    g.split(x, x, xi, 8).vectorize(xi);
    f.compute_at(g, xi).vectorize(x);

    Buffer<int> result = g.realize(width, height);

    // for (int iy = 0; iy < height; iy++) {
    //     for (int ix = 0; ix < width; ix++) {
    //         printf("%2d/%2d ", result(ix, iy), 2 * ix + 1 + 2 * iy);
    //     }
    //     printf("\n");
    // }
    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_2d_with_compute_at() {
    const int width = 35;
    const int height = 17;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi"), xii("xii");
    g.split(x, x, xi, 8, TailStrategy::GuardWithIf)
        .split(xi, xi, xii, 2, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(xii);
    f.compute_at(g, xii).vectorize(x);

    Buffer<int> result = g.realize(width, height);

    // for (int iy = 0; iy < height; iy++) {
    //     for (int ix = 0; ix < width; ix++) {
    //         printf("%2d/%2d ", result(ix, iy), 2 * ix + 1 + 2 * iy);
    //     }
    //     printf("\n");
    // }
    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int vectorize_all_d() {
    const int width = 12;
    const int height = 10;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 4, 2, TailStrategy::GuardWithIf)
        .vectorize(x)
        .vectorize(y)
        .vectorize(xi)
        .vectorize(yi);

    f.bound(x, 0, width).bound(y, 0, height);
    Buffer<int> result = f.realize(width, height);

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    if (check_image(result, cmp_func)) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (vectorize_2d_round_up()) {
        printf("vectorize_2d_round_up failed\n");
        return -1;
    }

    if (vectorize_2d_guard_with_if()) {
        printf("vectorize_2d_guard_with_if failed\n");
        return -1;
    }

    if (vectorize_2d_inlined_with_update()) {
        printf("vectorize_2d_inlined_with_update failed\n");
        return -1;
    }

    if (vectorize_2d_with_inner_for()) {
        printf("vectorize_2d_with_inner_for failed\n");
        return -1;
    }

    if (vectorize_2d_with_compute_at()) {
        printf("vectorize_2d_with_compute_at failed\n");
        return -1;
    }

    if (vectorize_2d_with_compute_at_vectorized()) {
        printf("vectorize_2d_with_compute_at_vectorized failed\n");
        return -1;
    }

    if (vectorize_all_d()) {
        printf("vectorize_all_d failed\n");
        return -1;
    }

    printf("Success\n");
    return 0;
}
