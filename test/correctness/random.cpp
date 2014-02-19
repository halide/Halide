#include <Halide.h>
#include <stdio.h>

using namespace Halide;


int main(int argc, char **argv) {
    Var x, y;

    const double tol = 0.001;

    {
        // Make a random image and check its statistics.
        Func f;
        f(x, y) = random_float();

        f.parallel(y);
        Image<float> rand_image = f.realize(1024, 1024);

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
            return -1;
        }

        if (fabs(variance - 1.0/12) > tol) {
            printf("Bad variance: %f\n", variance);
            return -1;
        }

        if (fabs(mean_dx) > tol) {
            printf("Bad mean_dx: %f\n", mean_dx);
            return -1;
        }

        if (fabs(variance_dx - 1.0/6) > tol) {
            printf("Bad variance_dx: %f\n", variance_dx);
            return -1;
        }

        if (fabs(mean_dy) > tol) {
            printf("Bad mean_dy: %f\n", mean_dy);
            return -1;
        }

        if (fabs(variance_dy - 1.0/6) > tol) {
            printf("Bad variance_dy: %f\n", variance_dy);
            return -1;
        }
    }


    // The same random seed should produce the same image, and
    // different random seeds should produce statistically independent
    // images.
    {
        Func f;
        f(x, y) = cast<double>(random_float());

        f.set_random_seed(0);

        f.debug_to_file("im1.tmp");
        Image<double> im1 = f.realize(1024, 1024);
        Image<double> im2 = f.realize(1024, 1024);

        Func g;
        g(x, y) = f(x, y);
        g.set_random_seed(1);
        g.debug_to_file("im3.tmp");
        Image<double> im3 = g.realize(1024, 1024);

        RDom r(im1);
        Expr v1 = im1(r.x, r.y);
        Expr v2 = im2(r.x, r.y);
        Expr v3 = im3(r.x, r.y);

        double e1 = evaluate<double>(sum(abs(v1 - v2))) / (1024 * 1024);
        double e2 = evaluate<double>(sum(abs(v1 - v3))) / (1024 * 1024);

        if (e1 != 0.0) {
            printf("The same random seed should produce the same image. "
                   "Instead the mean absolute difference was: %f\n", e1);
            return -1;
        }

        if (fabs(e2 - 1.0/3) > 0.01) {
            printf("Different random seeds should produce different images. "
                   "The mean absolute difference should be 1/3 but was %f\n", e2);
            return -1;
        }
    }

    return 0;
}

