#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    Target target_fuzzed = target.with_feature(Target::FuzzFloatStores);

    const int size = 1000;

    {
        // Check some code that should be unaffected
        Func f;
        Var x;
        f(x) = (x - 42.5f) / 16.0f;
        f.vectorize(x, 8);

        // Pipelines that only use a few significant bits of the float should be unaffected
        Image<float> im_ref = f.realize(size, target);
        Image<float> im_fuzzed = f.realize(size, target_fuzzed);

        for (int i = 0; i < im_ref.width(); i++) {
            // Test for exact floating point equality, which is exactly
            // the sort of thing FuzzFloatStores is trying to discourage.
            if (im_ref(i) != im_fuzzed(i)) {
                printf("Expected exact floating point equality between %10.10g and %10.10g\n", im_ref(i), im_fuzzed(i));
                return -1;
            }
        }
    }

    {
        // Check some code that should be affected
        Func f;
        Var x;
        f(x) = sqrt(x - 42.3333333f) / 17.0f - tan(x);
        f.vectorize(x, 8);

        // Pipelines that use all the bits should be wrong about half the time
        Image<float> im_ref = f.realize(size, target);
        Image<float> im_fuzzed = f.realize(size, target_fuzzed);

        int differences = 0;
        for (int i = 0; i < im_ref.width(); i++) {
            // Test for exact floating point equality, which is exactly
            // the sort of thing FuzzFloatStores is trying to discourage.
            if (im_ref(i) != im_fuzzed(i)) {
                differences++;
            }
        }

        if (differences == 0) {
            printf("fuzzing float stores should have done something\n");
            return -1;
        }

        // It should change the output about half the time. Assuming
        // it's sum of 'size' coin flips...
        int mean = size/2;
        int variance = size/4;
        int five_sigma = 5 * ceil(sqrt(variance));
        if (differences < mean - five_sigma || differences > mean + five_sigma) {
            printf("There were %d differences with floating point fuzzing on. Expected more like %d +/- %d.\n",
                   differences, mean, five_sigma);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
