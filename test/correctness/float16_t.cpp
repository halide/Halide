#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Buffer<float16_t> in1 = lambda(x, cast<float16_t>(x) / 8).realize(128);
    Buffer<bfloat16_t> in2 = lambda(x, cast<bfloat16_t>(x) / 8).realize(128);

    in2.for_each_element([&](int i) {
            bfloat16_t correct = Halide::bfloat16_t(i) / Halide::bfloat16_t(8.0f);
            if (in2(i) != correct) {
                printf("in2(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
                abort();
            }
        });

    Func wrap1, wrap2;
    wrap1(x) = in1(x);
    wrap2(x) = in2(x);

    Func f;
    f(x) = abs(sqrt(wrap1(x) * 4.0f) - sqrt(wrap2(x)) * 2.0f);

    f.compute_root().vectorize(x, 16);
    wrap1.compute_at(f, x).vectorize(x);
    wrap2.compute_at(f, x).vectorize(x);

    RDom r(0, 128);
    Func g;
    g() = maximum(cast<double>(f(r)));
    g.compile_to_assembly("/dev/stdout", {in1, in2}, Target("host-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_unroll-disable_llvm_loop_vectorize"));
    double d = evaluate<double>(g());
    if (d != 0) {
        printf("Should be zero: %f\n", d);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
