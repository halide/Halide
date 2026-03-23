#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    const double tol = 0.01;

    {
        // Make a random image and check its statistics.
        Func f;
        f(x, y) = random_float();
        f.vectorize(x, 4);
        f.parallel(y);
        Buffer<float> rand_image = f.realize({1024, 1024});

        // Do some tests for randomness.

        Func g;
        g(x, y) = cast<double>(rand_image(x, y));

        RDom r(rand_image);
        Expr val = g(r.x, r.y);

        double mean = evaluate<double>(sum(val)) / (1024 * 1024);
        double variance = evaluate<double>(sum(pow(val - (float)mean, 2))) / (1024 * 1024 - 1);

        // Also check the mean and variance of the gradient in x and y to check for pixel correlations.

        Expr dx = g(r.x, r.y) - g((r.x + 1) % 1024, r.y);
        Expr dy = g(r.x, r.y) - g(r.x, (r.y + 1) % 1024);

        double mean_dx = evaluate<double>(sum(dx)) / (1024 * 1024);
        double variance_dx = evaluate<double>(sum(pow(dx - (float)mean_dx, 2))) / (1024 * 1024 - 1);

        double mean_dy = evaluate<double>(sum(dy)) / (1024 * 1024);
        double variance_dy = evaluate<double>(sum(pow(dy - (float)mean_dy, 2))) / (1024 * 1024 - 1);

        if (fabs(mean - 0.5) > tol) {
            printf("Bad mean: %f\n", mean);
            return 1;
        }

        if (fabs(variance - 1.0 / 12) > tol) {
            printf("Bad variance: %f\n", variance);
            return 1;
        }

        if (fabs(mean_dx) > tol) {
            printf("Bad mean_dx: %f\n", mean_dx);
            return 1;
        }

        if (fabs(variance_dx - 1.0 / 6) > tol) {
            printf("Bad variance_dx: %f\n", variance_dx);
            return 1;
        }

        if (fabs(mean_dy) > tol) {
            printf("Bad mean_dy: %f\n", mean_dy);
            return 1;
        }

        if (fabs(variance_dy - 1.0 / 6) > tol) {
            printf("Bad variance_dy: %f\n", variance_dy);
            return 1;
        }
    }

    // The same random seed should produce the same image, and
    // different random seeds should produce statistically independent
    // images.
    {
        Param<int> seed;

        Func f;
        f(x, y) = cast<double>(random_float(seed));

        seed.set(0);

        Buffer<double> im1 = f.realize({1024, 1024});
        Buffer<double> im2 = f.realize({1024, 1024});

        Func g;
        g(x, y) = f(x, y);
        seed.set(1);

        Buffer<double> im3 = g.realize({1024, 1024});

        RDom r(im1);
        Expr v1 = im1(r.x, r.y);
        Expr v2 = im2(r.x, r.y);
        Expr v3 = im3(r.x, r.y);

        double e1 = evaluate<double>(sum(abs(v1 - v2))) / (1024 * 1024);
        double e2 = evaluate<double>(sum(abs(v1 - v3))) / (1024 * 1024);

        if (e1 != 0.0) {
            printf("The same random seed should produce the same image. "
                   "Instead the mean absolute difference was: %f\n",
                   e1);
            return 1;
        }

        if (fabs(e2 - 1.0 / 3) > 0.01) {
            printf("Different random seeds should produce different images. "
                   "The mean absolute difference should be 1/3 but was %f\n",
                   e2);
            return 1;
        }
    }

    // Test random ints as well.
    {
        Func f;
        f(x, y) = random_int();
        Buffer<int> im = f.realize({1024, 1024});

        // Count the number of set bits;
        RDom r(im);
        Expr val = f(r.x, r.y);

        int set_bits = evaluate<int>(sum(popcount(val)));

        // It should be that about half of them are set
        int correct = 512 * 1024 * 32;
        if (fabs(double(set_bits) / correct - 1) > tol) {
            printf("Set bits was %d instead of %d\n", set_bits, correct);
            return 1;
        }

        // Check to make sure adjacent bits are uncorrelated.
        Expr val2 = val ^ (val * 2);
        set_bits = evaluate<int>(sum(popcount(val2)));
        if (fabs(double(set_bits) / correct - 1) > tol) {
            printf("Set bits was %d instead of %d\n", set_bits, correct);
            return 1;
        }
    }

    // Check independence and dependence.
    {
        // Make two random variables
        Expr r1 = cast<double>(random_float());
        Expr r2 = cast<double>(random_float());

        Func f;
        f(x, y) = r1 + r1 - 1.0f;

        Func g;
        g(x, y) = r1 + r2 - 1.0f;

        // f is the sum of two dependent (equal) random variables, so should have variance 1/3
        // g is the sum of two independent random variables, so should have variance 1/6

        const int S = 1024;
        RDom r(0, S, 0, S);
        Expr f_val = f(r.x, r.y);
        Expr g_val = g(r.x, r.y);
        double f_var = evaluate<double>(sum(f_val * f_val)) / (S * S - 1);
        double g_var = evaluate<double>(sum(g_val * g_val)) / (S * S - 1);

        if (fabs(f_var - 1.0 / 3) > tol) {
            printf("Variance of f was supposed to be 1/3: %f\n", f_var);
            return 1;
        }

        if (fabs(g_var - 1.0 / 6) > tol) {
            printf("Variance of g was supposed to be 1/6 %f\n", g_var);
            return 1;
        }
    }

    printf("Success!\n");

    return 0;
}
