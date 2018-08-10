#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Fit a polynomial to sin from 0 to 1 using Halide's derivative support
    ImageParam coeffs(Float(64), 1);
    Param<double> learning_rate;
    Func approx_sin;
    Var x;

    Expr fx = x / cast<double>(1023);
    RDom r(coeffs);

    // We'll evaluate polynomial using Horner's method
    // FIXME: This non-commutative reduction actually causes a problem
    // for the current autodiff algorithm.
    // approx_sin(x) = cast<double>(0);
    // approx_sin(x) = (approx_sin(x)*fx + coeffs(coeffs.dim(0).max() - r));

    // Evaluate the polynomial directly
    approx_sin(x) = sum(pow(fx, r) * coeffs(r));

    Func exact_sin;
    exact_sin(x) = sin(fx);

    Func err;
    err(x) += pow(approx_sin(x) - exact_sin(x), 2);

    RDom d(0, 1024);
    Func total_err;
    total_err() = sum(err(d)) / 1024;

    // Take the derivative of the output w.r.t. the coefficients. The
    // returned object acts like a map from Funcs to the derivative of
    // the err w.r.t those Funcs.
    auto d_err_d = propagate_adjoints(total_err);

    // Compute the new coefficients in terms of the old.
    Func new_coeffs;
    new_coeffs(x) = coeffs(x) - learning_rate * d_err_d(coeffs)(x);

    // Schedule
    err.compute_root().vectorize(x, 4);
    new_coeffs.compute_root().vectorize(x, 4);
    approx_sin.compute_root().vectorize(x, 4);
    exact_sin.compute_root().vectorize(x, 4);

    // d_err_d(coeffs) is just a Func, and you can schedule it
    Var i = d_err_d(coeffs).args()[0];
    d_err_d(coeffs).compute_root().vectorize(i, 4).unroll(i);

    total_err.compute_root();

    // Not necessary, but makes the IR easier to read
    new_coeffs.bound(x, 0, 8);
    coeffs.dim(0).set_bounds(0, 8);

    // Gradient descent loop
    // Let's use an eighth-order polynomial
    Buffer<double> c(8);
    auto e = Buffer<double>::make_scalar();
    coeffs.set(c);
    Pipeline p({total_err, new_coeffs});
    c.fill(0);
    learning_rate.set(0.1);
    for (int i = 0; i <= 10000; i++) {
        bool should_print = (i == 0 || i == 10000);
        if (should_print) {
            printf("Coefficients: ");
            for (int j = 0; j < 8; j++) {
                printf("%f ", c(j));
            }
            printf("\n");
        }

        p.realize({e, c});

        if (should_print) {
            printf("Error: %f\n", e());
        }
    }

    if (e(0) < 0.0001f) {
        printf("Success!\n");
        return 0;
    } else {
        printf("Did not converge\n");
        return -1;
    }
}
