#include "Halide.h"
#include <assert.h>
#include <stdio.h>
#include <functional>
#include <map>
#include <numeric>

using std::map;
using std::vector;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int check_image(const Image<int> &im, const std::function<int(int,int)> &func) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = func(x, y);
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int vectorized_predicated_store_scalarized_predicated_load_test() {
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, 40, 0, 40);
    r.where(r.x + r.y < 24);

    ref(x, y) = 10;
    ref(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);
    Image<int> im_ref = ref.realize(80, 80);

    f(x, y) = 10;
    f(r.x, r.y) += g(2*r.x, r.y) + g(2*r.x + 1, r.y);
    f.update(0).vectorize(r.x, 8);

    Image<int> im = f.realize(80, 80);
    auto func = [&im_ref](int x, int y) { return im_ref(x, y); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int vectorized_dense_load_with_stride_minus_one_test() {
    int size = 50;
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    ref(x, y) = select(x < 23, g(size-x, y) * 2 + g(20-x, y), undef<int>());
    Image<int> im_ref = ref.realize(size, size);

    f(x, y) = select(x < 23, g(size-x, y) * 2 + g(20-x, y), undef<int>());
    f.vectorize(x, 8);

    Image<int> im = f.realize(size, size);
    auto func = [&im_ref](int x, int y) { return im_ref(x, y); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_vectorized_predicate_test() {
    int size = 50;
    Var x("x"), y("y");
    Func f ("f"), g("g"), ref("ref");

    g(x, y) = x * y;
    g.compute_root();

    RDom r(0, size, 0, size);
    r.where(r.x < 23);
    r.where(r.x*r.y + r.x*r.x < 300);

    ref(x, y) = 10;
    ref(r.x, r.y) = g(size-r.x, r.y) * 2 + g(20-r.x, r.y);
    Image<int> im_ref = ref.realize(size, size);

    f(x, y) = 10;
    f(r.x, r.y) = g(size-r.x, r.y) * 2 + g(20-r.x, r.y);
    f.update(0).vectorize(r.x, 8);

    Image<int> im = f.realize(size, size);
    auto func = [&im_ref](int x, int y) { return im_ref(x, y); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running vectorized predicated store scalarized predicated load test\n");
    if (vectorized_predicated_store_scalarized_predicated_load_test() != 0) {
        return -1;
    }

    printf("Running vectorized dense load with stride minus one test\n");
    if (vectorized_dense_load_with_stride_minus_one_test() != 0) {
        return -1;
    }

    printf("Running multiple vectorized predicate test\n");
    if (multiple_vectorized_predicate_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
