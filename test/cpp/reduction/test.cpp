#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Image<double> noise(32);
    for (int i = 0; i < 32; i++) {
      noise(i) = (double)rand() / RAND_MAX;
    }

    Func f1, f2, f3, f4, f5;
    RDom r(0, 32);
    f2(x) = sum(noise(r.x));
    f3(x) = product(noise(r.x));
    f4(x) = minimum(noise(r.x));
    f5(x) = maximum(noise(r.x));

    double s = 0, p = 1, mi = noise(0), ma = noise(0);
    for (int i = 0; i < 32; i++) {
        s += noise(i);
        p *= noise(i);
        mi = (noise(i) < mi ? noise(i) : mi);
        ma = (noise(i) > ma ? noise(i) : ma);
    }
    Image<double> im_sum = f2.realize(1);
    Image<double> im_prod = f3.realize(1);
    Image<double> im_min = f4.realize(1);
    Image<double> im_max = f5.realize(1);

    f5.compileToFile("f5");

    if (im_sum(0) != s) {
        printf("Sum was %f instead of %f\n", im_sum(0), s);
        return -1;
    }

    if (im_prod(0) != p) {
        printf("Product was %f instead of %f\n", im_prod(0), p);
        return -1;
    }

    if (im_min(0) != mi) {
        printf("Min was %f instead of %f\n", im_min(0), mi);
        return -1;
    }

    if (im_max(0) != ma) {
        printf("Max was %f instead of %f\n", im_max(0), ma);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
