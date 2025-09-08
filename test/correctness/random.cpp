#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(Random, RandomImageStatistics) {
    // Make a random image and check its statistics.
    Var x, y;

    const double tol = 0.01;

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

    ASSERT_NEAR(mean, 0.5, tol);
    ASSERT_NEAR(variance, 1.0 / 12, tol);
    ASSERT_NEAR(mean_dx, 0.0, tol);
    ASSERT_NEAR(variance_dx, 1.0 / 6, tol);
    ASSERT_NEAR(mean_dy, 0.0, tol);
    ASSERT_NEAR(variance_dy, 1.0 / 6, tol);
}

TEST(Random, SeedConsistency) {
    // The same random seed should produce the same image, and
    // different random seeds should produce statistically independent
    // images.
    Var x, y;
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

    ASSERT_EQ(e1, 0.0) << "The same random seed should produce the same image.";
    ASSERT_NEAR(e2, 1.0 / 3, 0.01) << "Different random seeds should produce different images.";
}

TEST(Random, RandomInts) {
    // Test random ints as well.
    Var x, y;
    Func f;
    f(x, y) = random_int();
    Buffer<int> im = f.realize({1024, 1024});

    // Count the number of set bits;
    RDom r(im);
    Expr val = f(r.x, r.y);

    int set_bits = evaluate<int>(sum(popcount(val)));

    // It should be that about half the bits are set
    int correct = 8 * im.size_in_bytes() / 2;
    double tol = 0.01;
    ASSERT_NEAR((double)set_bits / correct, 1.0, tol) << "Set bits was " << set_bits << " instead of " << correct;

    // Check to make sure adjacent bits are uncorrelated.
    Expr val2 = val ^ (val * 2);
    set_bits = evaluate<int>(sum(popcount(val2)));
    ASSERT_NEAR((double)set_bits / correct, 1.0, tol) << "Set bits was " << set_bits << " instead of " << correct;
}

TEST(Random, IndependenceAndDependence) {
    // Check independence and dependence.
    Var x, y;

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

    const double tol = 0.01;
    ASSERT_NEAR(f_var, 1.0 / 3, tol);
    ASSERT_NEAR(g_var, 1.0 / 6, tol);
}
