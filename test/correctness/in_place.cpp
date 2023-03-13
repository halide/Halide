#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    // Don't bother with a pure definition. Because this will be the
    // output stage, that means leave whatever's already in the output
    // buffer untouched.
    f(x) = undef<float>();

    // But do a sum-scan of it from 0 to 100
    RDom r(1, 99);
    f(r) += f(r - 1);

    // Make some test data.
    Buffer<float> data = lambda(x, sin(x)).realize({100});

    f.realize(data);

    // Do the same thing not in-place
    Buffer<float> reference_in = lambda(x, sin(x)).realize({100});
    Func g;
    g(x) = reference_in(x);
    g(r) += g(r - 1);
    Buffer<float> reference_out = g.realize({100});

    float err = evaluate_may_gpu<float>(sum(abs(data(r) - reference_out(r))));

    if (err > 0.0001f) {
        printf("Failed\n");
        return 1;
    }

    // Undef on one side of a select doesn't destroy the entire
    // select. Instead, it makes the containing store conditionally
    // not occur using an if statement. You probably shouldn't use
    // this feature. For one thing it vectorizes poorly (it reverts to
    // scalar code). This test does not exist in order to encourage
    // you to use this behavior. This just makes sure the expected
    // thing happens if someone is mad enough to write this.
    //
    // In general, it's better to use a completely undef pure case,
    // and then have an update step that loads the existing value and
    // stores it again unchanged at those pixels you don't want to
    // modify. However, this exists if you really need it. E.g. if one
    // page in the middle of your halide_buffer_t is memprotected as read
    // only and you can't store to it safely, or if you have some
    // weird memory mapping or race condition for which loading then
    // storing the same value has undesireable side-effects.

    // This sets the even numbered entires to 1.
    data = lambda(x, sin(x)).realize({100});
    Func h;
    h(x) = select(x % 2 == 0, 1.0f, undef<float>());
    h.vectorize(x, 4);
    h.realize(data);
    for (int x = 0; x < 100; x++) {
        double correct = sin((double)x);
        if (x % 2 == 0) {
            correct = 1.0;
        }
        if (fabs(data(x) - correct) > 0.001) {
            printf("data(%d) = %f instead of %f\n", x, data(x), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
