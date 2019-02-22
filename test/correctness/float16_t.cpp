#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Buffer<float16_t> in1 = lambda(x, cast<float16_t>(-0.5f) + cast<float16_t>(x) / (128)).realize(128);
    Buffer<bfloat16_t> in2 = lambda(x, cast<bfloat16_t>(-0.5f) + cast<bfloat16_t>(x) / (128)).realize(128);

    // Check the Halide-side math matches the C++-side math.
    in1.for_each_element([&](int i) {
            float16_t correct = Halide::float16_t(-0.5f) + Halide::float16_t(i) / Halide::float16_t(128.0f);
            if (in1(i) != correct) {
                printf("in1(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
                abort();
            }
        });

    in2.for_each_element([&](int i) {
            bfloat16_t correct = Halide::bfloat16_t(-0.5f) + Halide::bfloat16_t(i) / Halide::bfloat16_t(128.0f);
            if (in2(i) != correct) {
                printf("in2(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
                abort();
            }
        });

    Func wrap1, wrap2;
    wrap1(x) = in1(x);
    wrap2(x) = in2(x);

    Func f;
    f(x) = abs(sqrt(abs(wrap1(x) * 4.0f)) - sqrt(abs(wrap2(x))) * 2.0f);

    f.compute_root().vectorize(x, 16);
    wrap1.compute_at(f, x).vectorize(x);
    wrap2.compute_at(f, x).vectorize(x);

    RDom r(0, 128);
    Func g;
    g() = maximum(cast<double>(f(r)));

    double d = evaluate<double>(g());
    if (d != 0) {
        printf("Should be zero: %f\n", d);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
