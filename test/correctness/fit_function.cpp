#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Fit a polynomial to sin from 0 to 1 using Halide's derivative support
    ImageParam coeffs(Float(64), 1);
    Param<double> learning_rate;
    Func approx_sin;
    Var x, y;

    Expr fx = x / cast<double>(1023);
    RDom r(coeffs);
    Expr r_flipped = coeffs.dim(0).max() - r;

    // We'll evaluate polynomial using Horner's method. We need to
    // save the intermediate results for the backwards pass to use. It
    // is an error to ask for a derivative through a non-commutative
    // reduction, but non-commutative scans (which save all the
    // partial results) are fine. We'll leave the ultimate result at
    // index 0.
    approx_sin(x, y) = cast<double>(0);
    approx_sin(x, r_flipped) = approx_sin(x, r_flipped + 1)*fx + coeffs(r_flipped);

    // Evaluate the polynomial directly. This is a commutative
    // reduction, which is allowed.
    // approx_sin(x) = sum(pow(fx, r) * coeffs(r));

    Func exact_sin;
    exact_sin(x) = sin(fx);

    Func err;
    err(x) = pow(approx_sin(x, 0) - exact_sin(x), 2);

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
    approx_sin.compute_root().vectorize(x, 4).update().vectorize(x, 4);
    exact_sin.compute_root().vectorize(x, 4);
    total_err.compute_root();

    Var v;

    // d_err_d(coeffs) is just a Func, and you can schedule it. TODO:
    // Make it use the same variable names as the forward equivalent.
    /*
    Var v = d_err_d(coeffs).args()[0];
    d_err_d(coeffs).compute_root().vectorize(v, 4);
    */

    v = d_err_d(coeffs, -1, false).args()[0];
    d_err_d(coeffs, -1, false).compute_root().vectorize(v, 4);

    // Each stages of a Func with update stages gets a separate derivative Func.
    v = d_err_d(approx_sin, -1).args()[0];
    d_err_d(approx_sin, -1).compute_root().vectorize(v, 4);

    v = d_err_d(approx_sin, 0, false).args()[0];
    d_err_d(approx_sin, 0, false).compute_root().vectorize(v, 4);

    v = d_err_d(approx_sin, 0).args()[0];
    d_err_d(approx_sin, 0).compute_root().vectorize(v, 4);

    v = d_err_d(err, -1, false).args()[0];
    d_err_d(err, -1, false).compute_root().vectorize(v, 4);

    /*
    v = d_err_d(err, -1).args()[0];
    d_err_d(err, -1).compute_root().vectorize(v, 4);
    */

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
