#include "Halide.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

using namespace Halide;

int main(int argc, char **argv) {
    // Fit an odd polynomial to sin from 0 to pi/2 using Halide's derivative support
    ImageParam coeffs(Float(64), 1);
    Param<double> learning_rate;
    Param<int> order, samples;
    Func approx_sin;
    Var x, y;

    Expr fx = (x / cast<double>(samples)) * Expr(M_PI / 2);

    // We'll evaluate polynomial using a slightly modified Horner's
    // method. We need to save the intermediate results for the
    // backwards pass to use. We'll leave the ultimate result at index
    // 0.
    RDom r(0, order);
    Expr r_flipped = order - 1 - r;
    approx_sin(x, y) = cast<double>(0);
    approx_sin(x, r_flipped) = (approx_sin(x, r_flipped + 1) * fx + coeffs(r_flipped)) * fx;

    Func exact_sin;
    exact_sin(x) = sin(fx);

    // Minimize squared relative error. We'll be careful not to
    // evaluate it at zero. We're correct there by construction
    // anyway, because our polynomial is odd.
    Func err;
    err(x) = pow((approx_sin(x, 0) - exact_sin(x)) / exact_sin(x), 2);

    RDom d(1, samples - 1);
    Func average_err;
    average_err() = sum(err(d)) / samples;

    // Take the derivative of the output w.r.t. the coefficients. The
    // returned object acts like a map from Funcs to the derivative of
    // the err w.r.t those Funcs.
    auto d_err_d = propagate_adjoints(average_err);

    // Compute the new coefficients in terms of the old.
    Func new_coeffs;
    new_coeffs(x) = coeffs(x) - learning_rate * d_err_d(coeffs)(x);

    // Schedule
    err.compute_root().vectorize(x, 4);
    new_coeffs.compute_root().vectorize(x, 4);
    approx_sin.compute_root().vectorize(x, 4).update().vectorize(x, 4);
    exact_sin.compute_root().vectorize(x, 4);
    average_err.compute_root();

    // d_err_d(coeffs) is just a Func, and you can schedule it.
    // Each Func in the forward pipeline has a corresponding
    // derivative Func for each update, including the pure definition.
    // Here we will write a quick-and-dirty autoscheduler for this
    // pipeline to illustrate how you can access the new synthesized
    // derivative Funcs.
    Var v;
    Func fs[] = {coeffs, approx_sin, err};
    for (Func f : fs) {
        // Schedule the derivative Funcs for this Func.
        // For each Func we need to schedule all its updates.
        // update_id == -1 represents the pure definition.
        for (int update_id = -1; update_id < f.num_update_definitions(); update_id++) {
            Func df = d_err_d(f, update_id);
            df.compute_root().vectorize(df.args()[0], 4);
            for (int i = 0; i < df.num_update_definitions(); i++) {
                // Find a pure var to vectorize over
                for (auto d : df.update(i).get_schedule().dims()) {
                    if (d.is_pure()) {
                        df.update(i).vectorize(Var(d.var), 4);
                        break;
                    }
                }
            }
        }
    }

    // Gradient descent loop
    // Let's use eight terms and a thousand samples
    const int terms = 8;
    Buffer<double> c(terms);
    order.set(terms);
    samples.set(1000);
    auto e = Buffer<double>::make_scalar();
    coeffs.set(c);
    Pipeline p({average_err, new_coeffs});
    c.fill(0);
    // Initialize to the Taylor series for sin about zero
    c(0) = 1;
    for (int i = 1; i < terms; i++) {
        c(i) = -c(i - 1) / (i * 2 * (i * 2 + 1));
    }

    // This gradient descent is not particularly well-conditioned,
    // because the standard polynomial basis is nowhere near
    // orthogonal over [0, pi/2]. This should probably use a Cheychev
    // basis instead. We'll use a very slow learning rate and lots of
    // steps.
    learning_rate.set(0.00001);
    const int steps = 10000;
    double initial_error = 0.0;
    for (int i = 0; i <= steps; i++) {
        bool should_print = (i == 0 || i == steps / 2 || i == steps);
        if (should_print) {
            printf("Iteration %d\n"
                   "Coefficients: ",
                   i);
            for (int j = 0; j < terms; j++) {
                printf("%g ", c(j));
            }
            printf("\n");
        }

        p.realize({e, c});

        if (should_print) {
            printf("Err: %g\n", e());
        }

        if (i == 0) {
            initial_error = e();
        }
    }

    double final_error = e();
    if (final_error <= 1e-10 && final_error < initial_error) {
        printf("[fit_function] Success!\n");
        return 0;
    } else {
        printf("Did not converge\n");
        return 1;
    }
}
